<?php
class DataMarketAPI {
    private $conf;
    public function __construct($h, $p, $c, $k) { $this->conf = ["host"=>$h, "port"=>$p, "contract"=>$c, "priv"=>$k]; }

    public function __call($name, $args) {
        $payload = json_encode(["conf" => $this->conf, "method" => $name, "args" => $args]);
        $process = proc_open("./python3.10/bin/python3 bridge.py", [0=>["pipe","r"], 1=>["pipe","w"], 2=>["pipe","w"]], $pipes);
        if (is_resource($process)) {
            fwrite($pipes[0], $payload); fclose($pipes[0]);
            $out = stream_get_contents($pipes[1]); $err = stream_get_contents($pipes[2]);
            fclose($pipes[1]); fclose($pipes[2]); proc_close($process);
            if (preg_match('/\{"status".*\}/', $out, $m)) {
                $res = json_decode($m[0], true);
                if ($res['status'] === 'ok') return $res['data'];
                throw new Exception($res['message']);
            }
            throw new Exception("Bridge Error: $out $err");
        }
    }
}

$api = new DataMarketAPI("35.197.170.240", 23001, "f5c35074b7583ec006300e9f47d93cb45c486309", "c75f8d9b2a6bc0fe68eac7fef67c6b6f7c4f85163d58829b59110ff9e9210848");

try {
    // 1. 生成 ID
    $testIdRaw = "php_full_test_" . time();
    $id = $api->generateId($testIdRaw);
    echo "1. 生成 ID: $id\n";

    // 2. 发起创建交易
    echo "2. 创建数据...\n";
    $api->createData($id, $testIdRaw, 1);
    echo "3. 修改数据...\n";
    $api->updateData($id, '{"enabled":true,"preferred":0,"defaultOn":false,"presets":[{"key":"I1","value":"-Ku -a3 -An -s2 -s3+s -r3 -d3+s -d4 -d5 -s5 -d6 -s6 -s8 -r8 -s9 -Qr -Mh -At -s2+s -r2 -o2+s -d2 -s4 -d5 -q5+s -r5 -d6 -s7 -d7 -At,s -s2+s -r2 -d2 -s4 -d5 -r5 -d6 -s7 -d7"}]}');
    
    // // 3. 执行所有查询
    // echo "--- 查询结果 ---\n";
    // echo "Total Count: " . $api->getDataCount() . "\n";
    // echo "Price: " . $api->getPrice($id) . "\n";
    // echo "Owner: " . $api->dataOwner($id) . "\n";
    
    // echo "\nLatest Records (Top 3):\n";
    // print_r(array_slice($api->getAllLatestRecords(0, 10), 0, 3));

    echo "\nHistory:\n";
    print_r($api->getHistory($id));

} catch (Exception $e) {
    echo "\n运行错误: " . $e->getMessage() . "\n";
}
