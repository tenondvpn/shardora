import requests
import json
import os
import csv
import xml.etree.ElementTree as ET
import pandas as pd
from Crypto.Hash import keccak
import base64
import re


ret_list = [
["NC000", "包含个人敏感信息", "身份证号", "包含连续的18位数字，前6位表示区域代码，最后一位数字可能是字母X，疑似身份证号"],
["NC001", "包含个人敏感信息", "移动电话号码", "包含11位的数字序列，通常以13, 14, 15, 17, 18, 19开头，可选带国际区号+86，疑似移动电话号码"],
["NC002", "包含个人敏感信息", "银行卡账号", "包含连续的16-19位数字，数字之间没有特殊字符，疑似银行卡账号"],
["NC003", "包含个人敏感信息", "邮箱地址", "包含格式为example@domain.com或者是example@domain.cn的内容，并且domain字段通常为使用广泛的邮箱服务商"],
["NC004", "包含个人敏感信息", "中国大陆车牌号", "包含中国大陆车牌号，中国大陆车牌号通常由字母和数字组成，长度为7位，其中第一个字符为字母，代表省份，后续字符包含数字和字母的组合"],
["NC005", "包含个人敏感信息", "家庭住址", "包含省、市、区、街道、门牌号等信息，常见关键词有：省、市、区、县、路、街、巷、号等，疑似家庭住址"],
["NC006", "包含个人敏感信息", "姓名", "包含疑似常见的中文以及英文人名"],
["NC100", "包含生物识别信息", "面部识别特征", "包含面部特征图像文件（例如 jpg、png 等格式）或其特征向量"],
["NC101", "包含生物识别信息", "指纹", "包含指纹图像"],
["NC102", "包含生物识别信息", "掌纹", "包含掌纹图像"],
["NC103", "包含生物识别信息", "虹膜", "包含虹膜图像"],
["NC200", "包含违规内容", "危害国家安全", "包含涉及危害国家安全、泄露国家秘密、颠覆国家政权等的关键词"],
["NC201", "包含违规内容", "损害国家荣誉和利益", "包含损害国家荣誉的关键词"],
["NC202", "包含违规内容", "英雄烈士侮辱", "包含歪曲或亵渎英雄烈士的内容"],
["NC203", "包含违规内容", "恐怖主义", "包含恐怖主义相关的内容"],
["NC204", "包含违规内容", "极端主义", "包含极端主义相关的内容"],
["NC205", "包含违规内容", "色情", "包含色情内容"],
["NC206", "包含违规内容", "犯罪", "包含暴力，犯罪相关的内容"]
]


llm_client_id = "oDs7MT62pnTzRbZ6V5799Zur"
llm_client_secret = "lkTiUdjYdDjkZtMVzobbd2us9qmCqVxz"
censor_client_id = "L5C789JX3GcWYcHgRGRduAxt"
censor_client_secret = "cdlanGugNA3S1Vw8mo46EzidQ8TYKfak"


exts_to_text = {".txt", ".json", ".csv", ".xml", ".xls", ".xlsx", ".md"}
exts_to_image = {".jpg", ".png", ".pdf"}


class ComplianceAgent:
    def __init__(self, llm_client_id, llm_client_secret, censor_client_id, censor_client_secret):
        self.llm_client_id = llm_client_id
        self.llm_client_secret = llm_client_secret
        self.censor_client_id = censor_client_id
        self.censor_client_secret = censor_client_secret


    def format_convert(self, file_path):
        # 以下5行代码和compliance_process函数的开头有点重复，后期优化时可以调整一下
        if not os.path.exists(file_path):
            raise FileNotFoundError(f"文件 {file_path} 不存在")
        _, file_extension = os.path.splitext(file_path)
        file_extension = file_extension.lower()
        if file_extension in exts_to_text:
            if file_extension == '.txt':
                with open(file_path, 'r', encoding='utf-8') as file:
                    return file.read()

            elif file_extension == '.json':
                with open(file_path, 'r', encoding='utf-8') as file:
                    data = json.load(file)
                    return json.dumps(data, indent=4, ensure_ascii=False)

            elif file_extension == '.csv':
                with open(file_path, 'r') as file:
                    reader = csv.reader(file)
                    rows = [", ".join(row) for row in reader]
                    return "\n".join(rows)

            elif file_extension == '.xml':
                tree = ET.parse(file_path)
                root = tree.getroot()
                return ET.tostring(root, encoding='unicode', method='xml')

            elif file_extension in ['.xls', '.xlsx']:
                df = pd.read_excel(file_path)
                return df.to_string(index=False)

            elif file_extension == '.md':
                with open(file_path, 'r', encoding='utf-8') as file:
                    return file.read()

        else:
            raise ValueError(f"文件不支持转换成文本：{file_extension}")
        

    def get_text_hash(self, text_content):
        k = keccak.new(digest_bits=256)
        k.update(text_content.encode('utf-8'))
        hash_bytes = k.digest()
        hash_str = "0x" + hash_bytes.hex()
        return hash_str
    

    def llm_text(self, user_content, call_times):
        token_url = "https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=" + llm_client_id + "&client_secret=" + llm_client_secret
        token_payload = json.dumps("")
        token_headers = {
            'Content-Type': 'application/json',
            'Accept': 'application/json'
        }
        token_response = requests.request("POST", token_url, headers=token_headers, data=token_payload)
        token = token_response.json().get("access_token")
        s_pro = "\\n你是一个内容合规性审查专家，对上述内容进行合规性审查，并且按照以下要求返回审查结果\\n"
        for i in range(len(ret_list)):
            s_pro = s_pro + "如果" + ret_list[i][3] + "，则返回：" + ret_list[i][0] + "，" + ret_list[i][1] + "，" + ret_list[i][2] + "\\n"
        s_pro = s_pro + "包含几项就返回几项，要全面，除了我要求返回的内容之外，不要返回其他多余的内容，在给我的回答中首先返回以NC开头的状态码"
        content = user_content + s_pro
        llm_url = "https://aip.baidubce.com/rpc/2.0/ai_custom/v1/wenxinworkshop/chat/ernie-speed-128k?access_token=" + token
        payload = json.dumps({
            "messages": [
                {
                    "role": "user",
                    "content": content
                }
            ]
        })
        headers = {
            'Content-Type': 'application/json'
        }
        status_codes = set()
        # response_list存储每次调用大模型之后大模型返回的内容
        response_list = []
        for i in range(call_times):
            response = requests.request("POST", llm_url, headers=headers, data=payload)
            response_data = response.json()
            response_list.append(response_data['result'])
            matches = re.findall(r'NC\d{3}', response_data['result'])
            if matches:
                status_codes.update(matches)
        return status_codes, response_list


    def censor_image(self, file_path):
        token_url = "https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=" + censor_client_id + "&client_secret=" + censor_client_secret
        token_payload = ""
        token_headers = {
            'Content-Type': 'application/json',
            'Accept': 'application/json'
        }
        token_response = requests.request("POST", token_url, headers=token_headers, data=token_payload)
        token = token_response.json().get("access_token")
        censor_url = "https://aip.baidubce.com/rest/2.0/solution/v1/img_censor/v2/user_defined"
        # 二进制方式打开图片文件
        f = open(file_path, 'rb')
        img = base64.b64encode(f.read())
        params = {"image":img}
        request_url = censor_url + "?access_token=" + token
        headers = {'content-type': 'application/x-www-form-urlencoded'}
        response = requests.post(request_url, data=params, headers=headers)
        if response:
            print(response.json()) 


    def compliance_process(self, file_path, call_times=3):
        if not os.path.exists(file_path):
            raise FileNotFoundError(f"文件 {file_path} 不存在")
        _, file_extension = os.path.splitext(file_path)
        file_extension = file_extension.lower()
        if file_extension in exts_to_text:
            user_content = self.format_convert(file_path)
            text_hash = self.get_text_hash(user_content)
            status_codes, response_list = self.llm_text(user_content, call_times)
            print(f"数据哈希是 {text_hash}")
            print(f"状态码是 {status_codes}")
            print(f"下面打印每次调用大模型的检测结果")
            # for r in response_list:
            #     print(f"第{r}次检测结果")
            #     print(r)
            for i, r in enumerate(response_list):
                print(f"第{i + 1}次检测结果")
                print(r)
        elif file_extension in exts_to_image:
            self.censor_image(file_path)
        else:
            print("数据格式不符合要求")


'''
文本审核接口
'''
# request_url = "https://aip.baidubce.com/rest/2.0/solution/v1/text_censor/v2/user_defined"

# params = {"text":"不要侮辱伟大的乐侃"}
# access_token = '[调用鉴权接口获取的token]'
# request_url = request_url + "?access_token=" + access_token
# headers = {'content-type': 'application/x-www-form-urlencoded'}
# response = requests.post(request_url, data=params, headers=headers)
# if response:
#     print(response.json())


if __name__ == '__main__':
    agent = ComplianceAgent(llm_client_id, llm_client_secret, censor_client_id, censor_client_secret)
    file_path = "D:/PHD/Experiment/DataCompliance/测试用例007.json"
    agent.compliance_process(file_path, 5)