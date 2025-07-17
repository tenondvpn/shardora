from rocksdict import Rdict, Options, AccessType
import struct
import logging
import sys
from google.protobuf import json_format
sys.path.append('/root/shardora/src/')
from protos.view_block_pb2 import ViewBlockItem
import shardora_api

path = str("/root/zjnodes/s3_1/db/")

opt = Options(raw_mode=True)
opt.set_max_background_jobs(4)
opt.set_write_buffer_size(1024 * 1024 * 256)
opt.create_if_missing(True)
opt.set_keep_log_file_num(1)
opt.set_optimize_filters_for_hits(True)
opt.optimize_for_point_lookup(1024)
opt.set_max_open_files(1000)
opt.set_wal_size_limit_mb(100)
opt.set_wal_ttl_seconds(180)
opt.set_max_total_wal_size(67108864)

K_BLOCK_PREFIX = b"k\x01"
K_BLOCK_HEIGHT_PREFIX = b"j\x01"

def get_block_hash_with_height(
        db: Rdict,
        sharding_id: int,
        pool_index: int,
        height: int) -> bytes:
    # 构建键名
    key = K_BLOCK_HEIGHT_PREFIX + struct.pack("<IIQ", sharding_id, pool_index, height)
    
    try:
        # 从数据库获取哈希值
        block_hash = db[key]
        print(f"get sync key value {sharding_id}_{pool_index}_{height}, "
                     f"success get block with height: {sharding_id}, {pool_index}, {height} hash: {block_hash}")
        return block_hash
    except KeyError:
        print(f"failed get sync key value {sharding_id}_{pool_index}_{height}, "
                     f"success get block with height: {sharding_id}, {pool_index}, {height}")
        return None
    
def get_block(db: Rdict, block_hash: bytes, block_proto) -> bool:
    # 组合完整键名
    key = K_BLOCK_PREFIX + block_hash
    
    try:
        # 从数据库获取数据
        block_str = db[key]
        print(f"success get block: {block_hash}, str: {block_str}\n")
    except KeyError:
        print(f"failed get block: {block_str}")
        return False
    
    # 解析protobuf消息
    try:
        block_proto.ParseFromString(block_str)
        json_str = json_format.MessageToJson(block_proto)
        print(f"success parse: {block_proto}, json: {json_str}")
        return True
    except Exception:
        print("failed parse!")
        return False
        
    
def get_block_with_height(
        db: Rdict,
        sharding_id: int,
        pool_index: int,
        height: int,
        block_proto) -> bool:
    # 先获取block_hash
    block_hash = get_block_hash_with_height(db, sharding_id, pool_index, height)
    if not block_hash:
        return False
    
    # 再获取block内容
    return get_block(db, block_hash, block_proto)

if __name__ == "__main__":
    # 发送一笔交易
    shardora_api.transfer("")
    db = Rdict(path, options=opt, access_type=AccessType.read_only(False))
    print(f"hello world: 0")
    block_proto = ViewBlockItem()
    print(f"hello world: 1")
    get_block_with_height(db, 3, 0, 2, block_proto)
