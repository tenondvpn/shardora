import sys
import os
import json

# 添加搜索路径
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

try:
    from bridge_core import DataMarketFullClient
except Exception as e:
    sys.stdout.write(json.dumps({"status": "error", "message": str(e)}))
    sys.exit(1)

def main():
    try:
        raw_input = sys.stdin.read().strip()
        if not raw_input: return
        
        cmd = json.loads(raw_input)
        cli = DataMarketFullClient(cmd['conf']["host"], cmd['conf']["port"], cmd['conf']["contract"], cmd['conf']["priv"])
        
        # 预处理参数：将 0x 开头的 66 位字符串转为 bytes (bytes32)
        args = []
        for a in cmd['args']:
            if isinstance(a, str) and a.startswith("0x") and len(a) == 66:
                args.append(bytes.fromhex(a[2:]))
            else:
                args.append(a)
        
        # 动态调用
        method = cmd['method']
        if hasattr(cli, method):
            result = getattr(cli, method)(*args)
            sys.stdout.write(json.dumps({"status": "ok", "data": result}))
        else:
            sys.stdout.write(json.dumps({"status": "error", "message": f"Method {method} not found"}))
            
    except Exception as e:
        sys.stdout.write(json.dumps({"status": "error", "message": str(e)}))

if __name__ == "__main__":
    main()
