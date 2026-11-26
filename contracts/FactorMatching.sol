// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

/**
 * 要素链上匹配模块 (用例 159-162)
 * 159 数据要素匹配 / 160 模型要素匹配 / 161 算力要素匹配(含时段占用) / 162 多模态协同任务
 */
contract FactorMatching {
    // ------------------------------------------------------------------ 类型
    enum FactorType { Data, Model, Compute }
    enum RecordKind { Call, Auth, Settle }
    enum TaskStatus { None, Matched, Confirmed, Completed, Settled }

    struct DataFactor {
        address owner;
        bytes32 category;   // 数据类别
        bytes32 region;     // 地域
        bytes32 format;     // 数据格式
        uint64 size;        // 数据量(MB)
        uint32 quality;     // 质量分 0-100
        uint256 price;
        bool active;
    }

    struct ModelFactor {
        address owner;
        bytes32 taskType;   // 任务类型(分类/检测/...)
        bytes32 framework;  // 框架
        bytes32 version;    // 版本/哈希，用于核对链上登记
        uint32 accuracy;    // 精度 万分比
        uint32 paramsM;     // 参数量(百万)
        uint256 price;
        bool active;
    }

    struct ComputeFactor {
        address owner;
        uint32 cpuCores;
        uint32 gpuCount;
        uint32 memGB;
        uint64 availFrom;   // 可用窗口
        uint64 availTo;
        uint256 pricePerHour;
        bool active;
    }

    struct Slot { uint64 start; uint64 end; uint256 taskId; }

    // 需求(0 表示不限；bytes32(0) 表示不限)
    struct DataReq {
        bytes32 category; bytes32 region; bytes32 format;
        uint64 minSize; uint32 minQuality; uint256 maxPrice;
    }
    struct ModelReq {
        bytes32 taskType; bytes32 framework;
        uint32 minAccuracy; uint32 maxParamsM; uint256 maxPrice;
    }
    struct ComputeReq {
        uint32 minCpu; uint32 minGpu; uint32 minMemGB;
        uint64 start; uint64 end; uint256 maxPricePerHour;
    }

    struct Task {
        address requester;
        DataReq dReq; ModelReq mReq; ComputeReq cReq;
        uint256 dataId; uint256 modelId; uint256 computeId;
        uint256 totalCost;
        bytes32 resultHash;
        TaskStatus status;
    }

    struct Record {
        RecordKind kind;
        FactorType factorType;
        uint256 factorId;
        address party;
        uint256 amount;
        uint64 ts;
    }

    // ------------------------------------------------------------------ 存储
    DataFactor[] public dataFactors;
    ModelFactor[] public modelFactors;
    ComputeFactor[] public computeFactors;
    mapping(uint256 => Slot[]) private _slots;   // computeId => 已占用时段

    Task[] private _tasks;
    mapping(uint256 => Record[]) private _records; // taskId => 记录

    // ------------------------------------------------------------------ 事件
    event FactorRegistered(FactorType indexed t, uint256 indexed id, address owner);
    event FactorDeactivated(FactorType indexed t, uint256 indexed id);
    event Matched(FactorType indexed t, address indexed requester, uint256[] ids);
    event TaskMatched(uint256 indexed taskId, uint256 dataId, uint256 modelId, uint256 computeId, uint256 totalCost);
    event TaskConfirmed(uint256 indexed taskId);
    event TaskCompleted(uint256 indexed taskId, bytes32 resultHash);
    event TaskSettled(uint256 indexed taskId, uint256 totalCost);

    // ------------------------------------------------------------------ 登记
    function registerData(
        bytes32 category, bytes32 region, bytes32 format,
        uint64 size, uint32 quality, uint256 price
    ) external returns (uint256 id) {
        require(quality <= 100, "quality>100");
        id = dataFactors.length;
        dataFactors.push(DataFactor(msg.sender, category, region, format, size, quality, price, true));
        emit FactorRegistered(FactorType.Data, id, msg.sender);
    }

    function registerModel(
        bytes32 taskType, bytes32 framework, bytes32 version,
        uint32 accuracy, uint32 paramsM, uint256 price
    ) external returns (uint256 id) {
        id = modelFactors.length;
        modelFactors.push(ModelFactor(msg.sender, taskType, framework, version, accuracy, paramsM, price, true));
        emit FactorRegistered(FactorType.Model, id, msg.sender);
    }

    function registerCompute(
        uint32 cpuCores, uint32 gpuCount, uint32 memGB,
        uint64 availFrom, uint64 availTo, uint256 pricePerHour
    ) external returns (uint256 id) {
        require(availTo > availFrom, "bad window");
        id = computeFactors.length;
        computeFactors.push(ComputeFactor(msg.sender, cpuCores, gpuCount, memGB, availFrom, availTo, pricePerHour, true));
        emit FactorRegistered(FactorType.Compute, id, msg.sender);
    }

    function deactivate(FactorType t, uint256 id) external {
        if (t == FactorType.Data) {
            require(dataFactors[id].owner == msg.sender, "not owner");
            dataFactors[id].active = false;
        } else if (t == FactorType.Model) {
            require(modelFactors[id].owner == msg.sender, "not owner");
            modelFactors[id].active = false;
        } else {
            require(computeFactors[id].owner == msg.sender, "not owner");
            computeFactors[id].active = false;
        }
        emit FactorDeactivated(t, id);
    }

    // ------------------------------------------------------------------ 单项判定
    function _dataOk(DataFactor storage f, DataReq memory r) internal view returns (bool) {
        return f.active
            && (r.category == 0 || f.category == r.category)
            && (r.region == 0 || f.region == r.region)
            && (r.format == 0 || f.format == r.format)
            && f.size >= r.minSize
            && f.quality >= r.minQuality
            && (r.maxPrice == 0 || f.price <= r.maxPrice);
    }

    function _modelOk(ModelFactor storage f, ModelReq memory r) internal view returns (bool) {
        return f.active
            && (r.taskType == 0 || f.taskType == r.taskType)
            && (r.framework == 0 || f.framework == r.framework)
            && f.accuracy >= r.minAccuracy
            && (r.maxParamsM == 0 || f.paramsM <= r.maxParamsM)
            && (r.maxPrice == 0 || f.price <= r.maxPrice);
    }

    function _slotFree(uint256 id, uint64 s, uint64 e) internal view returns (bool) {
        Slot[] storage ss = _slots[id];
        for (uint256 i = 0; i < ss.length; i++) {
            if (s < ss[i].end && ss[i].start < e) return false; // 区间重叠
        }
        return true;
    }

    function _computeOk(uint256 id, ComputeReq memory r) internal view returns (bool) {
        ComputeFactor storage f = computeFactors[id];
        return f.active
            && r.end > r.start
            && f.cpuCores >= r.minCpu
            && f.gpuCount >= r.minGpu
            && f.memGB >= r.minMemGB
            && f.availFrom <= r.start && f.availTo >= r.end
            && (r.maxPricePerHour == 0 || f.pricePerHour <= r.maxPricePerHour)
            && _slotFree(id, r.start, r.end);
    }

    // ------------------------------------------------------------------ 159/160/161 匹配(只读)
    function matchData(DataReq memory r) public view returns (uint256[] memory out) {
        uint256 n = dataFactors.length;
        uint256[] memory tmp = new uint256[](n);
        uint256 c;
        for (uint256 i = 0; i < n; i++) if (_dataOk(dataFactors[i], r)) tmp[c++] = i;
        out = _trim(tmp, c);
    }

    function matchModel(ModelReq memory r) public view returns (uint256[] memory out) {
        uint256 n = modelFactors.length;
        uint256[] memory tmp = new uint256[](n);
        uint256 c;
        for (uint256 i = 0; i < n; i++) if (_modelOk(modelFactors[i], r)) tmp[c++] = i;
        out = _trim(tmp, c);
    }

    function matchCompute(ComputeReq memory r) public view returns (uint256[] memory out) {
        uint256 n = computeFactors.length;
        uint256[] memory tmp = new uint256[](n);
        uint256 c;
        for (uint256 i = 0; i < n; i++) if (_computeOk(i, r)) tmp[c++] = i;
        out = _trim(tmp, c);
    }

    // 上链留痕版本：执行匹配并发出事件，便于审计
    function submitDataMatch(DataReq calldata r) external returns (uint256[] memory ids) {
        ids = matchData(r);
        emit Matched(FactorType.Data, msg.sender, ids);
    }
    function submitModelMatch(ModelReq calldata r) external returns (uint256[] memory ids) {
        ids = matchModel(r);
        emit Matched(FactorType.Model, msg.sender, ids);
    }
    function submitComputeMatch(ComputeReq calldata r) external returns (uint256[] memory ids) {
        ids = matchCompute(r);
        emit Matched(FactorType.Compute, msg.sender, ids);
    }

    function _trim(uint256[] memory a, uint256 c) private pure returns (uint256[] memory o) {
        o = new uint256[](c);
        for (uint256 i = 0; i < c; i++) o[i] = a[i];
    }

    // ------------------------------------------------------------------ 162 协同任务
    function createTask(DataReq calldata d, ModelReq calldata m, ComputeReq calldata c)
        external returns (uint256 taskId)
    {
        require(c.end > c.start, "bad period");
        taskId = _tasks.length;
        Task storage t = _tasks.push();
        t.requester = msg.sender;
        t.dReq = d; t.mReq = m; t.cReq = c;
    }

    /// 三类要素同时匹配，选取各类中价格最低者组成组合方案
    function matchTask(uint256 taskId) external returns (uint256 dId, uint256 mId, uint256 cId, uint256 cost) {
        Task storage t = _tasks[taskId];
        require(t.status == TaskStatus.None || t.status == TaskStatus.Matched, "state");

        uint256[] memory ds = matchData(t.dReq);
        uint256[] memory ms = matchModel(t.mReq);
        uint256[] memory cs = matchCompute(t.cReq);
        require(ds.length > 0 && ms.length > 0 && cs.length > 0, "no full combination");

        dId = ds[0];
        for (uint256 i = 1; i < ds.length; i++)
            if (dataFactors[ds[i]].price < dataFactors[dId].price) dId = ds[i];
        mId = ms[0];
        for (uint256 i = 1; i < ms.length; i++)
            if (modelFactors[ms[i]].price < modelFactors[mId].price) mId = ms[i];
        cId = cs[0];
        for (uint256 i = 1; i < cs.length; i++)
            if (computeFactors[cs[i]].pricePerHour < computeFactors[cId].pricePerHour) cId = cs[i];

        uint256 hrs = (uint256(t.cReq.end - t.cReq.start) + 3599) / 3600;
        cost = dataFactors[dId].price + modelFactors[mId].price + computeFactors[cId].pricePerHour * hrs;

        t.dataId = dId; t.modelId = mId; t.computeId = cId;
        t.totalCost = cost;
        t.status = TaskStatus.Matched;
        emit TaskMatched(taskId, dId, mId, cId, cost);
    }

    /// 确认方案：占用算力时段，并记录三类要素授权
    function confirmTask(uint256 taskId) external {
        Task storage t = _tasks[taskId];
        require(msg.sender == t.requester, "not requester");
        require(t.status == TaskStatus.Matched, "state");
        require(_computeOk(t.computeId, t.cReq), "compute unavailable");
        require(dataFactors[t.dataId].active && modelFactors[t.modelId].active, "factor inactive");

        _slots[t.computeId].push(Slot(t.cReq.start, t.cReq.end, taskId));
        t.status = TaskStatus.Confirmed;

        _rec(taskId, RecordKind.Auth, FactorType.Data, t.dataId, dataFactors[t.dataId].owner, 0);
        _rec(taskId, RecordKind.Auth, FactorType.Model, t.modelId, modelFactors[t.modelId].owner, 0);
        _rec(taskId, RecordKind.Auth, FactorType.Compute, t.computeId, computeFactors[t.computeId].owner, 0);
        emit TaskConfirmed(taskId);
    }

    /// 算力方提交计算结果哈希(可与本地同条件计算结果哈希比对)，记录三类要素调用
    function completeTask(uint256 taskId, bytes32 resultHash) external {
        Task storage t = _tasks[taskId];
        require(t.status == TaskStatus.Confirmed, "state");
        require(msg.sender == computeFactors[t.computeId].owner, "not compute owner");
        t.resultHash = resultHash;
        t.status = TaskStatus.Completed;

        _rec(taskId, RecordKind.Call, FactorType.Data, t.dataId, msg.sender, 0);
        _rec(taskId, RecordKind.Call, FactorType.Model, t.modelId, msg.sender, 0);
        _rec(taskId, RecordKind.Call, FactorType.Compute, t.computeId, msg.sender, 0);
        emit TaskCompleted(taskId, resultHash);
    }

    /// 结算：按要素分别记录应付金额(账务记录，不转移资产)
    function settleTask(uint256 taskId) external {
        Task storage t = _tasks[taskId];
        require(msg.sender == t.requester, "not requester");
        require(t.status == TaskStatus.Completed, "state");
        t.status = TaskStatus.Settled;

        uint256 hrs = (uint256(t.cReq.end - t.cReq.start) + 3599) / 3600;
        _rec(taskId, RecordKind.Settle, FactorType.Data, t.dataId, dataFactors[t.dataId].owner, dataFactors[t.dataId].price);
        _rec(taskId, RecordKind.Settle, FactorType.Model, t.modelId, modelFactors[t.modelId].owner, modelFactors[t.modelId].price);
        _rec(taskId, RecordKind.Settle, FactorType.Compute, t.computeId, computeFactors[t.computeId].owner,
            computeFactors[t.computeId].pricePerHour * hrs);
        emit TaskSettled(taskId, t.totalCost);
    }

    function _rec(uint256 taskId, RecordKind k, FactorType ft, uint256 fid, address party, uint256 amt) private {
        _records[taskId].push(Record(k, ft, fid, party, amt, uint64(block.timestamp)));
    }

    // ------------------------------------------------------------------ 查询
    function getTask(uint256 taskId) external view returns (Task memory) { return _tasks[taskId]; }
    function getRecords(uint256 taskId) external view returns (Record[] memory) { return _records[taskId]; }
    function getSlots(uint256 computeId) external view returns (Slot[] memory) { return _slots[computeId]; }
    function counts() external view returns (uint256, uint256, uint256, uint256) {
        return (dataFactors.length, modelFactors.length, computeFactors.length, _tasks.length);
    }
}
