#!/usr/bin/env python3
# coding=utf-8
"""databaas-plus 流水线平台 API 客户端。

    BASE = http://47.111.109.8:28001   UI + /api 反代
    API  = http://47.111.109.8:28001/api  ->  容器内 27001 (Django/DRF)

鉴权: JWT (simplejwt)。POST /rest_token/ 换 access token，之后所有
/pipeline/* 请求带 Authorization: JWT <token>，有效期 30 天。

用法:
    python databaas_pipeline_client.py login
    python databaas_pipeline_client.py list-pipelines
    python databaas_pipeline_client.py show <pipeline_id>
    python databaas_pipeline_client.py update <task_id> --set pod_name=xxx
    python databaas_pipeline_client.py run <task_id> [--runtime 2026092412]
    python databaas_pipeline_client.py run-pipeline <pipeline_id>
    python databaas_pipeline_client.py status <pipeline_id> [--runtime ...]
    python databaas_pipeline_client.py watch <pipeline_id> [--interval 10]
"""
import argparse
import getpass
import hashlib
import json
import os
import sys
import time

import requests

HOST = os.environ.get("DATABAAS_HOST", "47.111.109.8")
PORT = os.environ.get("DATABAAS_PORT", "28001")
BASE = f"http://{HOST}:{PORT}"
API = f"{BASE}/api"

TOKEN_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".databaas_token")


def md5x3(text):
    """前端登录前对密码做的变换: md5(md5(md5(plain))) 的小写十六进制。

    服务端存的就是这个值，所以接口要传变换后的串，不是明文。
    """
    return hashlib.md5(
        hashlib.md5(hashlib.md5(text.encode()).hexdigest().encode())
        .hexdigest().encode()
    ).hexdigest()

# horae/tools_util.py TaskState
STATUS_TEXT = {
    0: "等待中",
    1: "运行中",
    2: "执行成功",
    3: "执行失败",
    4: "已超时",
    5: "已就绪(等待下发)",
    6: "被用户停止",
    7: "前置任务失败",
}

# 接口侧的 retry_count 选项 -> 库里存的值 (views.retry_count_num)
RETRY_API_TO_DB = {1: 0, 2: 1, 3: 5, 4: -1}
RETRY_DB_TO_API = {0: 1, 1: 2, 5: 3, -1: 4}

# 接口侧的 priority 选项 -> 语义 (horae/forms.py TaskForm.priority_choice)
PRIORITY_TEXT = {6: "P4", 7: "P3", 8: "P2", 9: "P1", 10: "P0"}


class DatabaasPipeline:
    def __init__(self, base=API, token=None):
        self.base = base.rstrip("/")
        self.s = requests.Session()
        if token:
            self.set_token(token)

    # ---------------- 鉴权 ----------------

    def set_token(self, token):
        # 服务端是 simplejwt，认证类 JWTAuthentication 只认 "Bearer <token>"。
        # 传 "JWT <token>" 会得到 401 Authentication credentials were not provided.
        self.s.headers["Authorization"] = f"Bearer {token}"

    def login(self, username, password, raw_password=False, save=True):
        """POST /rest_token/ -> {'access': ..., 'refresh': ...}

        password 默认按明文传入，内部做 md5x3 变换；已经变换过的串传
        raw_password=True。

        注意: /pipeline/get_pipelines/ 这类接口没有 @permission_classes，
        不带 token 也能读；但 get_tasks / update_task / run_one_task 等
        都必须带 access token。
        """
        payload = password if raw_password else md5x3(password)
        r = self.s.post(
            f"{self.base}/rest_token/",
            json={"username": username, "password": payload},
            timeout=20,
        )
        r.raise_for_status()
        data = r.json()
        self.set_token(data["access"])
        if save:
            with open(TOKEN_FILE, "w", encoding="utf-8") as fh:
                json.dump({"username": username, **data}, fh)
        return data

    @classmethod
    def from_saved_token(cls, path=TOKEN_FILE):
        with open(path, encoding="utf-8") as fh:
            return cls(token=json.load(fh)["access"])

    def _post(self, path, payload=None):
        # 必须表单编码: 视图读的是 request.POST，JSON body 会全部取到 None，
        # 然后 int(None) 直接 500。
        r = self.s.post(f"{self.base}{path}", data=payload or {}, timeout=60)
        r.raise_for_status()
        return r.json()

    def _get(self, path, params=None):
        r = self.s.get(f"{self.base}{path}", params=params or {}, timeout=60)
        r.raise_for_status()
        return r.json()

    # ---------------- 查询 ----------------

    def get_pipelines(self):
        """POST /pipeline/get_pipelines/ -> {'status':0,'pipeline_list':[{'id','name'}]}"""
        return self._post("/pipeline/get_pipelines/")

    def get_tasks(self, pipeline_id):
        """POST /pipeline/get_tasks/ {'pipeline_id': id}

        返回 task_list / edges / pipe_id / pipe_usr_graph。
        """
        return self._post("/pipeline/get_tasks/", {"pipeline_id": pipeline_id})

    def get_task_detail(self, task_id):
        """GET /pipeline/get_task_detail/<task_id>/

        返回 task(含 config/prev_task_ids/priority/over_time/retry_count/
        server_tag/version_id)、config(渲染后模板)、pipeline、processor、
        rely_tasks、server_tag、version_list。改参数前先读这个。
        """
        return self._get(f"/pipeline/get_task_detail/{task_id}/")

    # ---------------- 改参数 ----------------

    def update_task(self, task_id, *, name=None, config=None, template=None,
                    prev_task_ids=None, description=None, retry_count=None,
                    over_time=None, priority=None, server_tag=None,
                    task_type=None, version_id=None, use_processor=1):
        """POST /pipeline/update_task/<task_id>/

        form-data，字段对齐 horae/forms.py TaskForm:
            name            任务名(必填)
            config          参数键值对, 形如 "k1=v1\\nk2=v2" (必填)
            template        脚本模板, 可空
            prev_task_ids   前置任务 id, 逗号分隔
            description     描述
            retry_count     1=不重试 2=重试1次 3=重试5次 4=一直重试
                            (库里分别存 0/1/5/-1，务必用接口值, 客户端会做映射)
            over_time       超时秒数
            priority        5=P1 6=P4 7=P3 8=P2 9=P1 10=P0
            server_tag      资源标签, 'ALL' 表示不限
            type            1=python 2=spark 3=oozie 4=odps 5=shell
                            6=docker 7=clickhouse 8=v100 -1=stream
            version_id       绑定的处理器版本, 0=当前版本
            use_processor   是否使用处理器包

        未传的字段用当前值回填——后台是 TaskForm(request.POST, instance=task)
        的全量表单，缺字段会被判为无效。
        """
        cur = self.get_task_detail(task_id)["task"]
        if retry_count is not None:
            retry_field = retry_count          # 调用方传接口值 1..4
        else:
            retry_field = RETRY_DB_TO_API.get(
                cur.get("retry_count", 0), 1)  # 读回库里的值再翻成接口值
        payload = {
            "name": name if name is not None else cur.get("name", ""),
            "config": config if config is not None else cur.get("config", ""),
            "template": (template if template is not None
                         else cur.get("template") or ""),
            "prev_task_ids": (prev_task_ids if prev_task_ids is not None
                              else cur.get("prev_task_ids") or ""),
            "description": (description if description is not None
                            else cur.get("description") or ""),
            "retry_count": str(retry_field),
            "over_time": str(over_time if over_time is not None
                             else cur.get("over_time", 0)),
            "priority": str(priority if priority is not None
                            else cur.get("priority", 9)),
            "server_tag": (server_tag if server_tag is not None
                           else cur.get("server_tag") or "ALL"),
            "type": str(task_type if task_type is not None
                        else cur.get("proc_type", 1)),
            "version_id": str(version_id if version_id is not None
                              else cur.get("version_id", 0)),
            "use_processor": str(use_processor),
        }
        return self._post(f"/pipeline/update_task/{task_id}/", payload)

    # ---------------- 执行 ----------------

    def run_task(self, task_id, run_time=None):
        """POST /pipeline/run_one_task/ {'task_id_list': '1,2','run_time': '2026092412'}

        run_time 形如 YYYYMMDDHHMM (12 位, 精确到分钟)。省略时取当前分钟。
        """
        run_time = run_time or time.strftime("%Y%m%d%H%M")
        return self._post("/pipeline/run_one_task/",
                          {"task_id_list": str(task_id), "run_time": run_time})

    def run_pipeline(self, pipeline_id, run_time=None):
        """POST /pipeline/run_one_pipeline/ {'pl_id_list': str(id),'run_time': ...}"""
        run_time = run_time or time.strftime("%Y%m%d%H%M")
        return self._post("/pipeline/run_one_pipeline/",
                          {"pl_id_list": str(pipeline_id), "run_time": run_time})

    def stop_task(self, task_id, run_time):
        """POST /pipeline/stop_task/ {'task_id','run_time'}"""
        return self._post("/pipeline/stop_task/",
                          {"task_id": str(task_id), "run_time": run_time})

    def set_task_success(self, task_id, run_time):
        """POST /pipeline/history/set_task_success/ 手动置为成功"""
        return self._post("/pipeline/history/set_task_success/",
                          {"task_id": str(task_id), "run_time": run_time})

    # ---------------- 查执行状态 ----------------

    def get_graph(self, pipeline_id, run_time):
        """POST /pipeline/get_graph/ {'pipe_id','run_time'}

        单次运行里每个节点的状态 -> {'res': [{'task_id','status','run_time',...}]}
        status 见 STATUS_TEXT。轮询用这个。
        """
        return self._post("/pipeline/get_graph/",
                          {"pipe_id": str(pipeline_id), "run_time": run_time})

    def run_history(self, *, run_time=None, pl_name=None, task_name=None,
                    status=None, start=0, length=50, order_name="start_time",
                    order_type="desc", just_owner=1, history_type=0):
        """POST /pipeline/run_history/ DataTables 风格分页查历史

        返回 {'draw','recordsTotal','recordsFiltered','data':[...]}，
        每条含 task_id/run_time/pl_name/task_name/status/start_time/
        schedule_id/pl_id/use_time/cpu/mem/ret_code。

        just_owner=1 只查自己发起的，0 查全部；这两个参数在视图里被
        int() 强转，不能省，否则 500。
        """
        payload = {
            "draw": "1", "start": str(start), "length": str(length),
            "order_name": order_name, "order_type": order_type,
            "just_owner": str(just_owner), "type": str(history_type),
            "search_run_time": run_time or "",
            "search_pl_name": pl_name or "",
            "search_task_name": task_name or "",
            "search_status": str(status) if status is not None else "",
            "search_start_time": "", "search_end_time": "", "search_use_time": "",
        }
        return self._post("/pipeline/run_history/", payload)

    def get_task_log(self, schedule_id, subpath="", rerun_id=0):
        """POST /pipeline/get_task_log/ 列日志目录

        入参是 schedule_id（不是 pipe_id），subpath 形如 'results/'
        或 '' (根目录)。返回 {'list': [...], 'status': 0, 'info': ...}，
        list 里的目录项以 '/' 结尾。
        """
        return self._post("/pipeline/get_task_log/", {
            "schedule_id": str(schedule_id), "subpath": subpath,
            "rerun_id": str(rerun_id),
        })

    def get_log_content(self, schedule_id, file_name="result.md",
                        rerun_id=0):
        """POST /pipeline/get_log_content/ 读日志正文

        返回 {'file_content': ..., 'status': 0, 'len': N}。
        服务端固定最多读 10240 字节。
        """
        return self._post("/pipeline/get_log_content/", {
            "schedule_id": str(schedule_id), "file_name": file_name,
            "rerun_id": str(rerun_id),
        })

    # ---------------- 轮询 ----------------

    def watch(self, pipeline_id, run_time=None, interval=10, timeout=1800):
        """轮询 get_graph 直到所有任务终态。"""
        run_time = run_time or time.strftime("%Y%m%d%H%M")
        terminal = {2, 3, 4, 6, 7}
        deadline = time.time() + timeout
        while True:
            res = self.get_graph(pipeline_id, run_time)["res"]
            if not res:
                print(f"  run_time={run_time} 暂无记录, 等待调度...")
            else:
                for item in res:
                    st = item.get("status")
                    print(f"  task {item.get('task_id')} "
                          f"{item.get('task_name', '')} -> "
                          f"{STATUS_TEXT.get(st, st)}")
                if all(i.get("status") in terminal for i in res):
                    ok = all(i.get("status") == 2 for i in res)
                    return {"success": ok, "run_time": run_time, "tasks": res}
            if time.time() > deadline:
                raise TimeoutError(f"等待 {run_time} 超时, 最后状态: {res}")
            time.sleep(interval)


def parse_kv(text):
    """'a=1,b=2' 或 'a=1\\nb=2' -> 'a=1\\nb=2' (平台 config 的存储格式)"""
    if text is None:
        return None
    parts = [p for p in text.replace(",", "\n").split("\n") if p.strip()]
    return "\n".join(p.strip() for p in parts)


def main():
    ap = argparse.ArgumentParser(description="databaas-plus pipeline client")
    ap.add_argument("--user", default=os.environ.get("DATABAAS_USER", "shardora"))
    ap.add_argument("--password", default=os.environ.get("DATABAAS_PASSWORD"))
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("login")
    sub.add_parser("list-pipelines")

    p = sub.add_parser("show")
    p.add_argument("pipeline_id", type=int)

    p = sub.add_parser("task")
    p.add_argument("task_id", type=int)

    p = sub.add_parser("update")
    p.add_argument("task_id", type=int)
    p.add_argument("--set", dest="kv", required=True,
                   help="a=1,b=2 —— 覆盖 config 全量内容")
    p.add_argument("--name")
    p.add_argument("--priority", type=int)
    p.add_argument("--over-time", type=int)
    p.add_argument("--retry", type=int, choices=[1, 2, 3, 4])
    p.add_argument("--server-tag")
    p.add_argument("--description")
    p.add_argument("--template")

    p = sub.add_parser("run")
    p.add_argument("task_id", type=int)
    p.add_argument("--runtime")

    p = sub.add_parser("run-pipeline")
    p.add_argument("pipeline_id", type=int)
    p.add_argument("--runtime")

    p = sub.add_parser("status")
    p.add_argument("pipeline_id", type=int)
    p.add_argument("--runtime")

    p = sub.add_parser("history")
    p.add_argument("--pl-name")
    p.add_argument("--task-name")
    p.add_argument("--runtime")
    p.add_argument("--status", type=int)

    p = sub.add_parser("watch")
    p.add_argument("pipeline_id", type=int)
    p.add_argument("--runtime")
    p.add_argument("--interval", type=int, default=10)

    args = ap.parse_args()

    if args.cmd == "login":
        pw = args.password or getpass.getpass(f"{args.user} 密码: ")
        client = DatabaasPipeline()
        client.login(args.user, pw)
        print(f"OK, token 已写入 {TOKEN_FILE}")
        return

    if os.path.exists(TOKEN_FILE):
        client = DatabaasPipeline.from_saved_token()
    else:
        pw = args.password or getpass.getpass(f"{args.user} 密码: ")
        client = DatabaasPipeline()
        client.login(args.user, pw)

    dump = lambda d: print(json.dumps(d, ensure_ascii=False, indent=2))  # noqa: E731

    if args.cmd == "list-pipelines":
        dump(client.get_pipelines()["pipeline_list"])
    elif args.cmd == "show":
        dump(client.get_tasks(args.pipeline_id))
    elif args.cmd == "task":
        dump(client.get_task_detail(args.task_id))
    elif args.cmd == "update":
        res = client.update_task(
            args.task_id, name=args.name, config=parse_kv(args.kv),
            priority=args.priority, over_time=args.over_time,
            retry_count=args.retry, server_tag=args.server_tag,
            description=args.description, template=args.template,
        )
        dump(res)
    elif args.cmd == "run":
        dump(client.run_task(args.task_id, args.runtime))
    elif args.cmd == "run-pipeline":
        dump(client.run_pipeline(args.pipeline_id, args.runtime))
    elif args.cmd == "status":
        dump(client.get_graph(args.pipeline_id, args.runtime))
    elif args.cmd == "history":
        dump(client.run_history(run_time=args.runtime, pl_name=args.pl_name,
                                task_name=args.task_name, status=args.status))
    elif args.cmd == "watch":
        dump(client.watch(args.pipeline_id, args.runtime, args.interval))


if __name__ == "__main__":
    sys.exit(main())
