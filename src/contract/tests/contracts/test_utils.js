function hexToBytes(hex) {
    for (var bytes = [], c = 0; c < hex.length; c += 2)
        bytes.push(parseInt(hex.substr(c, 2), 16));
    return bytes;
}
function GetValidHexString(uint256_bytes) {
    var str_res = uint256_bytes.toString(16)
    while (str_res.length < 64) {
        str_res = "0" + str_res;
    }

    return str_res;
}
function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}
module.exports.hexToBytes = hexToBytes;
module.exports.GetValidHexString = GetValidHexString;
module.exports.sleep=sleep;