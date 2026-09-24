# databaas-plus 流水线接口说明

面向要写脚本对接流水线平台的人。所有内容都在服务器上实测过，标注为「实测」的行是真实抓到的响应。

## 1. 服务拓扑

```
浏览器 ──> http://47.111.109.8:28001/          nginx，托管前端静态资源（浙理工实验平台）
              └─ /api/  ──> 容器内 127.0.0.1:7001   Django + DRF
容器内 27001 ──> 同一个 Django（端口映射不同而已）
```

- **业务目录**：`/app`，Django 工程 `dags`，主要 app 是 `horae`
- **数据库**：MySQL，`Task` / `Pipeline` / `RunHistory` / `Schedule` 表在 `horae.models`
- **URL 前缀**：`dags/urls.py` 里 `re_path(r'^pipeline/', include('horae.urls'))`，所以业务接口是 `/pipeline/<name>/`

**访问入口用 `http://47.111.109.8:28001/api/pipeline/...`**，不要直接用 27001（容器内端口，外网不稳）。

## 2. 鉴权

DRF 只启用了 `rest_framework_simplejwt.authentication.JWTAuthentication`，全局默认 `IsAuthenticated`。

### 登录取 token

```http
POST /api/rest_token/
Content-Type: application/json

{"username": "shardora", "password": "25ab3b38f7afc116f18fa9821e44d561"}
```

```json
{"access": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...", "refresh": "eyJ..."}
```

`SIMPLE_JWT.ACCESS_TOKEN_LIFETIME = 30 天`，`REFRESH_TOKEN_LIFETIME = 5 天`。

三个必须知道的坑：

1. **密码不是明文**。前端在提交前做 `md5(md5(md5(password)))` 的三层 MD5，取小写十六进制，服务库里存的就是这个串。所以接口要传变换后的值。`shardora` 的明文是 `test`，变换后是 `25ab3b38f7afc116f18fa9821e44d561`。

2. **认证头是 `Bearer`，不是 `JWT`**。虽然是 simplejwt，但 `JWTAuthentication` 的 `AUTH_HEADER_TYPES` 没改成 `JWT`。实测：

   | 请求头 | 结果 |
   |---|---|
   | `Authorization: Bearer <access>` | `200` |
   | `Authorization: JWT <access>` | `401 Authentication credentials were not provided.` |

3. **请求体必须是表单编码，不能是 JSON**。视图统一读 `request.POST.get(...)`，发 JSON 时全部取到 `None`，紧接着 `int(None)` 直接 500。前端用的就是 `qs.stringify`（表单编码）。

### 哪些接口要 token

不是全部。`get_pipelines`、`search_pipeline` 这类没有 `@permission_classes` 装饰器，不带 token 也能读（实测匿名访问 `get_pipelines` 返回全量数据）。而 `get_tasks`、`get_task_detail`、`update_task`、`run_one_task`、`get_user_list` 都有 `@permission_classes([IsAuthenticated])`，必须带 token。

## 3. 接口清单

### 3.1 查流程列表

```http
POST /api/pipeline/get_pipelines/
Authorization: Bearer <access>
```

```json
{"status": 0, "pipeline_list": [{"id": 46, "name": "helloworld"}, ...]}
```

实测返回 127 条。不需要鉴权。

### 3.2 查流程下的任务和连线

```http
POST /api/pipeline/get_tasks/
Content-Type: application/x-www-form-urlencoded

pipeline_id=46
```

```json
{
  "status": 0,
  "task_list": [{"id": 142, "pl_id": 46, "pid": 20, "name": "helloworld",
                 "config": "pod_name=helloworld-1723\r\nimage_name=hello-world",
                 "priority": 6, "over_time": 30, "retry_count": 5,
                 "server_tag": "ALL", "version_id": 27, "proc_type": 6, ...}],
  "edges": [...],
  "pipe_id": 46,
  "pipe_usr_graph": ...
}
```

字段含义（`horae.models.Task`）：

| 字段 | 说明 |
|---|---|
| `pl_id` | 所属流程 id |
| `pid` | 绑定的处理器（processor）id |
| `config` | 任务参数，`key=value` 用 `\r\n` 分隔，**这里是改参数的落点** |
| `prev_task_ids` | 前置任务 id，逗号分隔，决定 DAG 依赖 |
| `retry_count` | 库里存 0（不重试）/1（1次）/5（5次）/-1（一直） |
| `over_time` | 超时秒数 |
| `priority` | 6=P4 7=P3 8=P2 9=P1 10=P0 |
| `server_tag` | 资源标签，`ALL` 为不限 |
| `version_id` | 绑定的处理器包版本 |
| `proc_type` | 处理器类型 1=python 2=spark 3=oozie 4=odps 5=shell 6=docker 7=clickhouse 8=v100 |

### 3.3 查任务详情（改参数前先读）

```http
GET /api/pipeline/get_task_detail/142/
Authorization: Bearer <access>
```

```json
{
  "status": 0,
  "task": {"id": 142, "pl_id": 46, "pid": 20, "name": "helloworld",
           "config": "pod_name=helloworld-1723\r\nimage_name=hello-world",
           "over_time": 30, "retry_count": 5, "priority": 6,
           "server_tag": "ALL", "version_id": 27, "proc_type": 6,
           "pipeline_name": "helloworld", "proc_name": "k8s测试"},
  "config": "pod_name=helloworld-1723\nimage_name=hello-world",
  "pipeline": {...}, "processor": {...},
  "rely_tasks": {}, "version_list": [...]
}
```

`task.config` 带 `\r\n`，顶层 `config` 是渲染后的展示串（`\n`）。**回写时用 `task.config` 的原值格式**。

### 3.4 改任务参数

```http
POST /api/pipeline/update_task/142/
Content-Type: application/x-www-form-urlencoded
Authorization: Bearer <access>

name=helloworld&config=pod_name%3Dnew-pod%0Aimage_name%3Dhello-world
&retry_count=3&over_time=60&priority=9&server_tag=ALL&type=6
&version_id=27&use_processor=1&prev_task_ids=&description=desc&template=
```

```json
{"status": 0, "msg": "OK", "task": {...}}
```

要点：

- 后台是 `TaskForm(request.POST, instance=task)` 的**全量表单**，缺字段会被判无效。所以脚本里未修改的字段要用当前值回填（demo 里的 `update_task` 已处理）。
- **`retry_count` 要传接口值**：1=不重试 2=重试1次 3=重试5次 4=一直。传库里的 `5` 会报 `Select a valid choice. 5 is not one of the available choices.` 服务端再映射回 0/1/5/-1。
- `config` 里 `\n` 和 `\r\n` 都能收，存进去是 `\r\n`。
- `type` 字段是 `ChoiceField`，传 `proc_type` 的值。
- 返回 `status=0` 即成功，`status=1` 时 `msg` 是表单校验错误文本。

### 3.5 执行任务

```http
POST /api/pipeline/run_one_task/
Authorization: Bearer <access>

task_id_list=142&run_time=2026092412
```

```json
{"status": 0, "msg": "OK"}
```

- `task_id_list` 逗号分隔，可一次跑多个：`142,143`。要么全成功要么全失败。
- `run_time` 格式 `YYYYMMDDHH`（小时粒度），实测库里存的是 12 位补零形式 `202409020000`。支持单值、逗号多值、区间 `2024090210-2024090310`，也支持天级/小时级后缀。
- 其他执行入口：
  - `POST /pipeline/run_one_pipeline/` — body `pl_id_list=<id>&run_time=...`，跑整条流程
  - `POST /pipeline/run_task_with_all_successors/` — body `task_id_list=<id>&run_time=...`，跑任务及所有下游
  - `POST /pipeline/run_some_task/` — body `tasklist=1,2&runtimelist=t1,t2`，逐个跑，可配不同 run_time
  - `POST /pipeline/stop_task/` — body `task_id=<id>&run_time=...` 停任务
  - `POST /pipeline/history/set_task_success/` — body `task_id=<id>&run_time=...` 手动置成功

> 注意：`run_one_task` 会真的往 k8s 提交 job，是真实副作用，脚本里别误触发。

### 3.6 查执行状态

**按 run_time 查某次运行的全图**（轮询用这个）：

```http
POST /api/pipeline/get_graph/
Authorization: Bearer <access>

pipe_id=46&run_time=202409020000
```

```json
{"res": [{"task_id": 142, "run_time": "202409020000", "pl_id": 46,
          "status": 6, "schedule_id": 400, "pl_name": "helloworld",
          "task_name": "helloworld", "begin_time": "2024-09-18 17:38:11",
          "retried_count": 0, "proc_id": 20, "proc_type": 6,
          "manager_list": [["shardora", ""]], "id": 142}]}
```

状态码（`horae/tools_util.py TaskState`）：

| 值 | 含义 |
|---|---|
| 0 | 等待中 |
| 1 | 运行中 |
| 2 | 执行成功 |
| 3 | 执行失败 |
| 4 | 已超时 |
| 5 | 已就绪（等待下发） |
| 6 | 被用户停止 |
| 7 | 前置任务失败 |

终态是 2/3/4/6/7，判断整条流程成功即所有节点 `status==2`。

**分页查历史记录**：

```http
POST /api/pipeline/run_history/
Authorization: Bearer <access>

draw=1&start=0&length=50&order_name=start_time&order_type=desc
&just_owner=1&type=0&search_run_time=&search_pl_name=&search_task_name=
&search_status=&search_start_time=&search_use_time=
```

```json
{"draw": 1, "recordsTotal": 44, "recordsFiltered": 44,
 "data": [{"id": 400, "task_id": 142, "run_time": "202409020000", "pl_id": 46,
           "start_time": "2024-09-18 17:38:11", "status": 6, "schedule_id": 400,
           "pl_name": "helloworld", "task_name": "helloworld",
           "use_time": 0, "cpu": 0, "mem": 0, "ret_code": 0}]}
```

- 过滤参数：`search_run_time` / `search_pl_name` / `search_task_name` / `search_status` / `search_start_time` / `search_use_time`（支持 `>=60`、`<300` 这类比较前缀）。
- **`just_owner` 和 `type` 必传**，服务端对它们做 `int()` 强转，缺了或传空串直接 500 TypeError（实测）。前端只传 `just_owner` 不传 `type`，所以浏览器上也会 500——这是服务端既有的 bug，脚本里绕过即可。
- `just_owner=1` 只查自己发起的，`0` 查全部。

### 3.7 查日志

```http
POST /api/pipeline/get_task_log/
Authorization: Bearer <access>

schedule_id=400&subpath=&rerun_id=0
```

返回 `{"list": ["result.md", "subdir/", ...], "status": 0, "info": ...}`，目录项以 `/` 结尾。**注意入参是 `schedule_id` 不是 `pipe_id`**（`schedule_id` 从 3.6 的响应里取）。

```http
POST /api/pipeline/get_log_content/
Authorization: Bearer <access>

schedule_id=400&file_name=result.md&rerun_id=0
```

返回 `{"file_content": "...", "status": 0, "len": N}`。服务端固定最多读 10240 字节。

## 4. Python demo

见同目录 `databaas_pipeline_client.py`，依赖 `requests`。

```bash
pip install requests

# 登录（密码传明文，内部做 md5x3）
python databaas_pipeline_client.py login

python databaas_pipeline_client.py list-pipelines
python databaas_pipeline_client.py show 46          # 流程下的任务
python databaas_pipeline_client.py task 142         # 任务详情

# 改参数
python databaas_pipeline_client.py update 142 --set pod_name=new-pod,image_name=hello-world

# 执行
python databaas_pipeline_client.py run 142
python databaas_pipeline_client.py run-pipeline 46

# 查状态
python databaas_pipeline_client.py status 46 --runtime 2026092412
python databaas_pipeline_client.py watch 46 --interval 10    # 轮询到终态
python databaas_pipeline_client.py history --task-name helloworld
```

作为库用：

```python
from databaas_pipeline_client import DatabaasPipeline, STATUS_TEXT

c = DatabaasPipeline()
c.login("shardora", "test", save=False)

# 改参数后执行，再轮询到结束
c.update_task(142, config="pod_name=my-pod\nimage_name=hello-world", over_time=60)
c.run_task(142, run_time="2026092412")
result = c.watch(46, run_time="2026092412")
print("成功:", result["success"])
```

## 5. 已知服务端问题（脚本里需绕开）

| 现象 | 原因 | 绕法 |
|---|---|---|
| 发 JSON body 全部字段为 null，然后 500 | 视图只读 `request.POST` | 用表单编码 |
| `Authorization: JWT <token>` 401 | `AUTH_HEADER_TYPES` 是默认的 `Bearer` | 用 `Bearer` |
| `run_history` 缺 `type` 就 500 TypeError | 视图对 `type` 做 `int()`，前端没传 | 显式传 `type=0` |
| `update_task` 报 `5 is not one of the available choices` | 表单要 1..4，库里是 0/1/5/-1 | 传接口值，客户端做映射 |
| `get_pipelines` 匿名可读 | 该视图没有 `@permission_classes` | 需要注意数据暴露面 |
