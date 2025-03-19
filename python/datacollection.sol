pragma solidity ^0.8.0;

contract DataCollection {
    struct UsageRecord {
        uint256 timestamp;        //时间戳
        string cpuModel;          //cpu型号
        uint256 cpuPercent;       //CPU使用率
        string gpuModel;          //gpu型号
        uint256 gpuPercent;       //gpu使用情况
        uint256 memorySize;       //内存大小
        uint256 memoryPercent;    //内存使用情况
        uint256 bandwidthMBps;    //带宽
    }

    UsageRecord[] public usageRecords;
    address public owner;

    constructor() {
        owner = msg.sender;
    }

    // 修饰器：限制只有管理员可以调用
    modifier onlyOwner() {
        require(msg.sender == owner, "Not an owner");
        _;
    }

    // 记录数据（保留 onlyOwner 限制）
    function recordUsage(
        uint256 timestamp,
        string memory cpuModel,
        uint256 cpuPercent,
        string memory gpuModel,
        uint256 gpuPercent,
        uint256 memorySize,
        uint256 memoryPercent,
        uint256 bandwidthMBps
    ) public onlyOwner {
        usageRecords.push(UsageRecord(timestamp, cpuModel,cpuPercent,gpuModel,gpuPercent,memorySize,memoryPercent, bandwidthMBps));
    }

    // 获取数组长度
    function getUsageRecordsLength() public view returns (uint256) {
        return usageRecords.length;
    }

    // 按索引查询单条记录
    function getUsageRecord(uint256 index) public view returns (uint256, string,uint256, string,uint256,uint256, uint256, uint256) {
        require(index < usageRecords.length, "Index out of bounds");
        UsageRecord memory record = usageRecords[index];
        return (record.timestamp,record.cpuModel, record.cpuPercent, record.gpuModel,record.gpuPercent,record.memorySize,record.memoryPercent, record.bandwidthMBps);
    }
    // 按时间范围查询（返回所有符合条件的记录索引）
    function getRecordsByTimeRange(uint256 startTime, uint256 endTime) public view returns (uint256[] memory) {
        uint256 count = 0;
        // 先计算符合条件的记录数
        for (uint256 i = 0; i < usageRecords.length; i++) {
            if (usageRecords[i].timestamp >= startTime && usageRecords[i].timestamp <= endTime) {
                count++;
            }
        }

        // 创建结果数组
        uint256[] memory indices = new uint256[](count);
        uint256 currentIndex = 0;
        for (uint256 i = 0; i < usageRecords.length; i++) {
            if (usageRecords[i].timestamp >= startTime && usageRecords[i].timestamp <= endTime) {
                indices[currentIndex] = i;
                currentIndex++;
            }
        }
        return indices;
    }
}