pragma solidity >=0.8.17 <0.9.0;
pragma experimental ABIEncoderV2;

interface DIDRegistry {
    function existsDID(string memory _did) external view returns (bool);
}

contract hashtree {

    // ========== 数据结构 ==========
    struct Asset {
        string assetId;          // 使用字符串存储原始值
        string ownerDID;
        string merkleRoot;
        string[] blockHashes;    // 分块哈希改为字符串
    }

    // ========== 存储结构 ==========
    mapping(string => Asset) private assets;         // 资产ID -> 资产详情
    mapping(string => string[]) private ownerAssets; // 所有者DID -> 资产ID列表

    // ========== 核心功能 ==========
    function registerAsset(
        string memory _ownerDID,
        string memory _assetId,
        string memory _merkleRoot,
        string[] memory _blockHashes
    ) public {

        // 存储原始字符串
        assets[_assetId] = Asset({
            assetId: _assetId,
            ownerDID: _ownerDID,
            merkleRoot: _merkleRoot,
            blockHashes: _blockHashes
        });

        // 更新用户资产列表
        ownerAssets[_ownerDID].push(_assetId);
    }


    function getAssetIdsByOwner(string memory _ownerDID)
    public
    view
    returns (string[] memory)
    {
        return ownerAssets[_ownerDID];
    }

    function getAssetDetails(string memory _assetId)
    public
    view
    returns (
        string memory ownerDID,
        string memory merkleRoot,
        string[] memory blockHashes
    )
    {
        Asset storage asset = assets[_assetId];
        return (asset.ownerDID, asset.merkleRoot, asset.blockHashes);
    }

    function verifyBlockHash(
        string memory _assetId,
        uint256 _blockIndex,
        string memory _expectedHash
    ) public view returns (bool) {
        Asset storage asset = assets[_assetId];
        require(_blockIndex < asset.blockHashes.length, "Invalid block index");
        return keccak256(abi.encodePacked(asset.blockHashes[_blockIndex])) ==
            keccak256(abi.encodePacked(_expectedHash));
    }

}