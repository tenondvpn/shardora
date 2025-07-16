import rocksdb
import os
import shutil # 用于清理，如果需要的话

# 定义数据库路径
kRocksDbDataPath = '/root/zjnodes/s3_1/db'

import rocksdb
import logging
from binascii import hexlify # 用于十六进制编码
# 假设你的 protobuf 定义文件名为 view_block_pb2.py
# 并且 ViewBlockItem 在其中定义
from view_block.protobuf import view_block_pb2 # 根据你的实际protobuf模块路径修改

# 配置日志 (模拟 ZJC_EMPTY_DEBUG)
# 在实际项目中，你可能在程序的入口处统一配置日志
logging.basicConfig(level=logging.logging.DEBUG, format='%(levelname)s: %(message)s')
logger = logging.getLogger(__name__)

# 定义常量 (对应 C++ 的 kBlockPrefix)
# 请替换为你的实际前缀
kBlockPrefix = b"block_prefix:" # RocksDB 键通常是 bytes 类型，所以这里也用 bytes

def get_block(db_instance: rocksdb.DB, block_hash: str) -> tuple[bool, view_block_pb2.ViewBlockItem | None]:
    """
    从 RocksDB 中获取 ViewBlockItem。

    Args:
        db_instance: 已打开的 rocksdb.DB 实例。
        block_hash: 块的哈希值字符串（通常是十六进制）。

    Returns:
        一个元组 (bool, ViewBlockItem 或 None)。
        如果成功获取并解析，返回 (True, ViewBlockItem 对象)。
        如果失败，返回 (False, None)。
    """
    # 将 block_hash 从十六进制字符串转换为原始字节串（如果它本身就是十六进制字符串的话）
    # 如果 block_hash 已经是原始字节串，可以跳过这一步
    try:
        # 假设 block_hash 是一个十六进制字符串，需要解码
        # 例如 "0a1b2c..." 转换为 b'\x0a\x1b\x2c...'
        # 如果 block_hash 直接就是原始字节，请根据实际情况调整
        decoded_block_hash = bytes.fromhex(block_hash)
    except ValueError:
        logger.error(f"Invalid block_hash format: {block_hash}")
        return False, None

    # 构建 key
    # Python 的 bytes 类型拼接
    key = kBlockPrefix + decoded_block_hash
    # C++ 中是 key.reserve(48) 预分配内存，Python 不需要手动做这个

    block_str_bytes = None
    try:
        # RocksDB 的 Get 方法返回 bytes 或 None
        block_str_bytes = db_instance.get(key)
    except rocksdb.RocksDBException as e:
        # 捕获 RocksDB 相关的错误，例如数据库损坏等
        logger.error(f"RocksDB error while getting block '{block_hash}': {e}")
        return False, None

    if block_str_bytes is None:
        # 模拟 ZJC_EMPTY_DEBUG("failed get view block: %s", common::Encode::HexEncode(block_hash).c_str());
        # 这里的 block_hash 已经是传入的原始字符串，直接用即可
        # 如果需要转换为十六进制打印，可以使用 hexlify(decoded_block_hash).decode('utf-8')
        logger.debug(f"Failed to get view block: {block_hash}")
        return False, None

    # 创建一个 Protobuf 消息实例
    block_item = view_block_pb2.ViewBlockItem()

    # 尝试解析从 RocksDB 读取到的字节串
    try:
        block_item.ParseFromString(block_str_bytes)
    except Exception as e:
        logger.error(f"Failed to parse ViewBlockItem from data for block '{block_hash}': {e}")
        return False, None

    return True, block_item

# --- 示例使用 ---
if __name__ == "__main__":
    # 假设你的 RocksDB 数据库路径
    db_path = './my_rocksdb_data_for_blocks_test'

    # 1. 创建或打开一个 RocksDB 实例 (用于写入和读取)
    # 在真实应用中，db_instance 会从其他地方传入
    if not os.path.exists(db_path):
        os.makedirs(db_path)
    
    # 初始化 RocksDB 选项
    options = rocksdb.Options()
    options.create_if_missing = True
    # 根据需要添加其他 RocksDB 选项

    db = rocksdb.DB(db_path, options)
    print(f"RocksDB 数据库已打开: {db_path}")

    # 2. 准备一个模拟的 ViewBlockItem 数据
    # 创建一个 Protobuf 消息实例并填充数据
    test_block = view_block_pb2.ViewBlockItem()
    test_block.header_hash = b'some_header_hash_bytes_123'
    test_block.height = 100
    test_block.timestamp = 1678886400 # 示例时间戳
    test_block.body = b'some_block_body_data'

    # 将 Protobuf 消息序列化为字节串
    serialized_block = test_block.SerializeToString()

    # 定义一个测试用的 block_hash (假设是十六进制字符串)
    # 这个哈希通常是根据区块内容计算出来的，这里只是一个示例
    test_block_hash = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789" # 示例64位哈希

    # 将区块数据写入 RocksDB
    # 注意：这里的 key 也是 kBlockPrefix + block_hash 的字节形式
    db.put(kBlockPrefix + bytes.fromhex(test_block_hash), serialized_block)
    print(f"模拟区块 '{test_block_hash}' 已写入数据库。")

    # 3. 使用 get_block 函数读取数据
    print("\n--- 尝试读取存在的区块 ---")
    success, retrieved_block = get_block(db, test_block_hash)
    if success:
        print(f"成功获取区块！高度: {retrieved_block.height}, 头部哈希: {retrieved_block.header_hash.hex()}")
        # 验证读取到的数据
        assert retrieved_block.height == test_block.height
        assert retrieved_block.header_hash == test_block.header_hash
        assert retrieved_block.body == test_block.body
        print("数据验证成功。")
    else:
        print("获取区块失败。")

    print("\n--- 尝试读取不存在的区块 ---")
    non_existent_hash = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    success, retrieved_block = get_block(db, non_existent_hash)
    if not success:
        print(f"成功检测到区块 '{non_existent_hash}' 不存在。")
    else:
        print("错误：意外地读取到了不存在的区块。")

    # 4. 关闭数据库
    del db
    print("\nRocksDB 数据库已关闭。")

    # 5. 清理测试数据 (可选)
    # import shutil
    # if os.path.exists(db_path):
    #     shutil.rmtree(db_path)
    #     print(f"已删除测试数据库目录: {db_path}")
    
def GetBlockWithHeight(network_id, pool_index, height):
    try:
        # 创建 Options 对象并设置 read_only=True
        opts = rocksdb.Options()
        opts.read_only = True # 关键设置
        # 以只读模式打开数据库
        db_read = rocksdb.DB(kRocksDbDataPath, opts)
        print(f"数据库 '{kRocksDbDataPath}' 已成功以只读模式打开。")

        # 尝试读取数据
        print("\n尝试读取数据:")
        value_fruit = db_read.get(b'fruit')
        if value_fruit:
            print(f"键 'fruit' 对应的值是: {value_fruit.decode('utf-8')}")
        else:
            print("键 'fruit' 不存在。")

        value_drink = db_read.get(b'drink')
        if value_drink:
            print(f"键 'drink' 对应的值是: {value_drink.decode('utf-8')}")
        else:
            print("键 'drink' 不存在。")

        # 尝试写入数据（只读模式下会失败）
        print("\n尝试在只读模式下写入数据 (预期会失败):")
        try:
            db_read.put(b'new_key', b'new_value')
            print("错误：意外地在只读模式下写入成功！")
        except rocksdb.RocksDBException as e:
            print(f"正如预期，写入操作失败：{e}")
            print("这证明数据库确实处于只读模式。")

    except rocksdb.RocksDBException as e:
        print(f"打开数据库失败：{e}")
    except Exception as e:
        print(f"发生其他错误：{e}")
    finally:
        # 确保只读模式的数据库也被关闭
        if 'db_read' in locals() and db_read:
            del db_read
            print("\n只读数据库已关闭。")
