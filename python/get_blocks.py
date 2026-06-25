import requests
import json

class ShardoraHttpClient:
    def __init__(self, host, port):
        self.base_url = f"http://{host}:{port}"

    def get_latest_pool_info(self, network):
        url = f"{self.base_url}/get_latest_pool_info"

        # 构造 POST 参数
        # 注意：C++ 代码中使用 get_param_value，这通常对应表单格式 (data=payload)
        payload = {
            "network": str(network),
        }

        try:
            # 发送 POST 请求
            # 使用 data=payload 发送 application/x-www-form-urlencoded 格式
            response = requests.post(url, data=payload, timeout=10)

            print(f"请求状态码: {response.status_code}")

            if response.status_code == 200:
                # 尝试解析 JSON
                try:
                    return response.json()
                except:
                    # 如果返回的是纯文本
                    return response.text
            else:
                return f"Error: {response.text}"

        except requests.exceptions.RequestException as e:
            return f"Connection Failed: {e}"

    def get_blocks(self, network, pool_index, height, count=1):
        """
        对应 C++ 的 svr.Post("/get_blocks", GetBlocks)
        """
        url = f"{self.base_url}/get_blocks"

        # 构造 POST 参数
        # 注意：C++ 代码中使用 get_param_value，这通常对应表单格式 (data=payload)
        payload = {
            "network": str(network),
            "pool_index": str(pool_index),
            "height": str(height),
            "count": str(count)
        }

        try:
            # 发送 POST 请求
            # 使用 data=payload 发送 application/x-www-form-urlencoded 格式
            response = requests.post(url, data=payload, timeout=10)

            print(f"请求状态码: {response.status_code}")

            if response.status_code == 200:
                # 尝试解析 JSON
                try:
                    return response.json()
                except:
                    # 如果返回的是纯文本
                    return response.text
            else:
                return f"Error: {response.text}"

        except requests.exceptions.RequestException as e:
            return f"Connection Failed: {e}"

    def get_blocks_by_hashes(self, hash_list):
        """
        通过 Hash 列表获取区块数据
        :param host: 服务器地址 (例如 '127.0.0.1')
        :param port: 端口 (例如 8080)
        :param hash_list: 十六进制哈希字符串列表
        """
        url = f"{self.base_url}/get_block_with_hash"

        # 构造 JSON 请求体
        payload = {
            "hash_list": hash_list
        }

        try:
            # 使用 json= 参数会自动设置 Content-Type: application/json
            # 并且将 dict 自动转换为 JSON 字符串
            response = requests.post(url, json=payload, timeout=15)

            print(f"请求发送至: {url}")
            print(f"响应状态码: {response.status_code}")

            if response.status_code == 200:
                result = response.json()
                if result.get("status") == 0:
                    print(f"成功获取 {len(result.get('blocks', []))} 个区块")
                    return result
                else:
                    print(f"业务错误: {result.get('error')}")
                    return result
            else:
                print(f"HTTP 错误: {response.text}")
                return None

        except requests.exceptions.RequestException as e:
            print(f"网络请求异常: {e}")
            return None

# --- 使用示例 ---
if __name__ == "__main__":
    # 配置你的服务器 IP 和端口（对应 Init 函数中的 ip, port）
    client = ShardoraHttpClient("104.198.109.193", 23080)

    # 调用接口
    res = client.get_latest_pool_info(network=3)

    # 格式化输出结果
    if isinstance(res, dict):
        print(json.dumps(res, indent=4, ensure_ascii=False))
    else:
        print(res)

    res = client.get_blocks(network=3, pool_index=13, height=0, count=32)
    # 格式化输出结果
    if isinstance(res, dict):
        print(json.dumps(res, indent=4, ensure_ascii=False))
    else:
        print(res)

    hashes_to_query = [
        "8fc02e2cf2fe1895e943c6aaa15f1186f8296f3840b4443c432083a2c9e170a7",
    ]

    # 3. 执行调用
    res = client.get_blocks_by_hashes(hashes_to_query)
    # 格式化输出结果
    if isinstance(res, dict):
        print(json.dumps(res, indent=4, ensure_ascii=False))
    else:
        print(res)
