package com.ruoyi.asset.util;

import com.ruoyi.asset.contract.AssetDID;
import com.ruoyi.asset.contract.BlockTrade;
import com.ruoyi.asset.contract.DIDProxyFactory;
import com.ruoyi.asset.contract.UserDID;
import org.bouncycastle.jcajce.provider.digest.Keccak;
import org.web3j.abi.FunctionEncoder;
import org.web3j.abi.datatypes.Address;
import org.web3j.abi.datatypes.Type;
import org.web3j.crypto.*;
import org.web3j.utils.Numeric;

import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;
import java.io.*;
import java.math.BigInteger;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.security.cert.X509Certificate;
import java.util.*;

/**
 * Shardora Native Deployer
 * ====================
 * Deploys contracts using Shardora's native /transaction API instead of
 * eth_sendRawTransaction. This matches the protocol used by shardora_sdk.py.
 *
 * Shardora differences from Ethereum:
 *   1. Uses /transaction POST endpoint (not JSON-RPC)
 *   2. Contract address = CREATE2: keccak256(0xff + sender + salt + codeHash)[-20:]
 *   3. Nonce queried via /query_account (not eth_getTransactionCount)
 *   4. Contract calls require gas prefund (step=7) before execution (step=8)
 *   5. Signature: sign(keccak256(nonce_le8 + pubkey_uncompressed + to + amount_le8
 *                       + gas_limit_le8 + gas_price_le8 + step_le8 + code/input [+ prefund_le8]))
 *   6. Transaction receipt via /transaction_receipt
 */
public class ShardoraDeployer {

    // Shardora node
    private static final String NODE_HOST = "35.197.170.240";
    private static final int    NODE_PORT = 23001;
    private static final String BASE_URL  = "https://" + NODE_HOST + ":" + NODE_PORT;

    // TODO: replace with your private key (64 hex chars, no 0x prefix)
    private static final String PRIVATE_KEY =
            "50903025c95cca01971c216f161c2a53b9e7d4a9fbe67c89adf038fe4eb82e5f";

    // Shardora step types (from shardora_sdk.py StepType)
    private static final int STEP_NORMAL_FROM         = 0;
    private static final int STEP_CREATE_CONTRACT     = 6;
    private static final int STEP_CONTRACT_GAS_PREFUND = 7;
    private static final int STEP_CONTRACT_EXECUTE    = 8;

    private static final long DEFAULT_GAS_LIMIT  = 5_000_000L;
    private static final long DEFAULT_GAS_PRICE  = 1L;
    private static final long DEFAULT_PREFUND    = 10_000_000L;
    private static final int  RECEIPT_TIMEOUT_SEC = 120;

    // ─── Entry Point ─────────────────────────────────────────────────────

    public static void main(String[] args) throws Exception {
        disableSslVerification();

        ECKeyPair keyPair = ECKeyPair.create(new BigInteger(PRIVATE_KEY, 16));
        // Uncompressed public key (64 bytes, no 0x04 prefix)
        byte[] pubBytes = getPubKeyUncompressed(keyPair);
        String senderAddr = deriveAddress(pubBytes);

        System.out.println("BASE_URL  = " + BASE_URL);
        System.out.println("sender    = " + senderAddr);

        // 1) Deploy UserDID
        String userImpl = deployContract(keyPair, pubBytes, senderAddr,
                UserDID.BINARY, Collections.<Type>emptyList(), "userDID");
        System.out.println("UserDID impl        = " + userImpl);

        // 2) Deploy AssetDID
        String assetImpl = deployContract(keyPair, pubBytes, senderAddr,
                AssetDID.BINARY, Collections.<Type>emptyList(), "assetDID");
        System.out.println("AssetDID impl       = " + assetImpl);

        // 3) Deploy DIDProxyFactory(userImpl, assetImpl)
        List<Type> factoryArgs = Arrays.<Type>asList(
                new Address(toChecksumAddr(userImpl)),
                new Address(toChecksumAddr(assetImpl))
        );
        String factoryAddr = deployContract(keyPair, pubBytes, senderAddr,
                DIDProxyFactory.BINARY, factoryArgs, "factory");
        System.out.println("FACTORY_ADDR        = " + factoryAddr);

        // 4) Deploy BlockTrade
        String tradeAddr = deployContract(keyPair, pubBytes, senderAddr,
                BlockTrade.BINARY, Collections.<Type>emptyList(), "trade");
        System.out.println("TRADE_ADDR          = " + tradeAddr);

        System.out.println("\n=== Configuration for business code ===");
        System.out.println("private static final String FACTORY_ADDR = \"" + factoryAddr + "\";");
        System.out.println("private static final String TRADE_ADDR   = \"" + tradeAddr + "\";");
    }

    // ─── Deploy Contract ─────────────────────────────────────────────────

    /**
     * Deploy a contract using Shardora's native protocol:
     *   1. Compute contract address via CREATE2
     *   2. Fetch sender nonce from /query_account
     *   3. Build tx hash: keccak256(nonce_le8 + pub + to + amount_le8 + gasLimit_le8
     *                              + gasPrice_le8 + step_le8 + bytecode [+ prefund_le8])
     *   4. Sign with ECDSA (deterministic, canonical)
     *   5. POST to /transaction
     *   6. Poll /transaction_receipt until confirmed
     */
    private static String deployContract(ECKeyPair keyPair, byte[] pubBytes,
                                         String senderAddr, String binary,
                                         List<Type> ctorArgs, String salt)
            throws Exception {

        String cleanBin = Numeric.cleanHexPrefix(binary);
        String encodedCtor = Numeric.cleanHexPrefix(FunctionEncoder.encodeConstructor(ctorArgs));
        String fullBytecode = cleanBin + encodedCtor;

        // Compute contract address: CREATE2(sender, salt, bytecode)
        String contractAddr = calcCreate2Address(senderAddr, salt, fullBytecode);
        System.out.println("\n[deploy " + salt + "] contract address = " + contractAddr);

        // Fetch nonce
        long nonce = fetchNonce(senderAddr) + 1;
        System.out.println("[deploy " + salt + "] nonce = " + nonce);

        // Build and send
        String txHash = buildAndSend(keyPair, pubBytes, nonce, contractAddr,
                0, STEP_CREATE_CONTRACT, fullBytecode, "", DEFAULT_PREFUND);
        System.out.println("[deploy " + salt + "] txHash = " + txHash);

        // Wait for receipt
        Map<String, Object> receipt = waitReceipt(txHash);
        if (receipt == null) {
            throw new RuntimeException("Deploy timeout for " + salt + ", txHash=" + txHash);
        }
        int status = getInt(receipt, "status", -1);
        if (status != 0) {
            throw new RuntimeException("Deploy failed for " + salt + ", status=" + status
                    + ", txHash=" + txHash);
        }

        System.out.println("[deploy " + salt + "] confirmed, status=0");
        return contractAddr;
    }

    // ─── Shardora Native Transaction ─────────────────────────────────────────

    /**
     * Build the Shardora transaction hash, sign it, and POST to /transaction.
     * Returns the hex-encoded tx hash.
     *
     * Hash preimage (matching shardora_sdk.py send_transaction_auto):
     *   nonce (uint64 LE) + pubkey (65 bytes uncompressed with 04 prefix)
     *   + to (20 bytes) + amount (uint64 LE) + gas_limit (uint64 LE)
     *   + gas_price (uint64 LE) + step (uint64 LE)
     *   + [contract_code bytes] + [input bytes] + [prefund uint64 LE]
     */
    private static String buildAndSend(ECKeyPair keyPair, byte[] pubBytes,
                                       long nonce, String to, long amount,
                                       int step, String contractCode,
                                       String inputHex, long prefund)
            throws Exception {

        // The Python SDK uses the raw 64-byte uncompressed key (no 0x04 prefix)
        // for the hash preimage, but sends the full uncompressed hex in the POST.
        String pubHex = Numeric.toHexStringNoPrefixZeroPadded(
                keyPair.getPublicKey(), 128); // 64 bytes = 128 hex chars

        ByteArrayOutputStream buf = new ByteArrayOutputStream();
        writeLe64(buf, nonce);
        buf.write(hexToBytes(pubHex));                     // 64 bytes pubkey (no prefix)
        buf.write(hexToBytes(to));                         // 20 bytes to address
        writeLe64(buf, amount);
        writeLe64(buf, DEFAULT_GAS_LIMIT);
        writeLe64(buf, DEFAULT_GAS_PRICE);
        writeLe64(buf, step);
        if (contractCode != null && !contractCode.isEmpty()) {
            buf.write(hexToBytes(contractCode));
        }
        if (inputHex != null && !inputHex.isEmpty()) {
            buf.write(hexToBytes(inputHex));
        }
        if (prefund > 0) {
            writeLe64(buf, prefund);
        }

        byte[] txHash = keccak256(buf.toByteArray());

        // Sign (deterministic, canonical — matching Python's sigencode_string_canonize)
        ECDSASignature sig = keyPair.sign(txHash);
        // Canonicalize s (low-s)
        if (sig.s.compareTo(Sign.CURVE.getN().shiftRight(1)) > 0) {
            sig = new ECDSASignature(sig.r, Sign.CURVE.getN().subtract(sig.s));
        }
        String signR = Numeric.toHexStringNoPrefixZeroPadded(sig.r, 64);
        String signS = Numeric.toHexStringNoPrefixZeroPadded(sig.s, 64);

        // POST to /transaction
        Map<String, String> params = new LinkedHashMap<>();
        params.put("nonce", String.valueOf(nonce));
        params.put("pubkey", pubHex);
        params.put("to", to);
        params.put("amount", String.valueOf(amount));
        params.put("gas_limit", String.valueOf(DEFAULT_GAS_LIMIT));
        params.put("gas_price", String.valueOf(DEFAULT_GAS_PRICE));
        params.put("shard_id", "0");
        params.put("type", String.valueOf(step));
        params.put("sign_r", signR);
        params.put("sign_s", signS);
        params.put("sign_v", "0");
        if (contractCode != null && !contractCode.isEmpty()) {
            params.put("bytes_code", contractCode);
        }
        if (inputHex != null && !inputHex.isEmpty()) {
            params.put("input", inputHex);
        }
        if (prefund > 0) {
            params.put("prefund", String.valueOf(prefund));
        }

        String body = postForm(BASE_URL + "/transaction", params);
        System.out.println("[send_tx] response: " + body.substring(0, Math.min(body.length(), 200)));

        // Retry on nonce invalid
        if (body.contains("kTxUserNonceInvalid")) {
            System.out.println("[send_tx] nonce invalid, retrying...");
            for (int retry = 0; retry < 30; retry++) {
                Thread.sleep(1000);
                long curNonce = fetchNonce(to.length() == 40 ? to : hexToBytes(to).length == 20 ? to : "") + 1;
                if (curNonce != nonce) {
                    nonce = curNonce;
                    // Rebuild hash and signature with new nonce
                    return buildAndSend(keyPair, pubBytes, nonce, to, amount,
                            step, contractCode, inputHex, prefund);
                }
            }
            throw new RuntimeException("Nonce retry exhausted");
        }

        return bytesToHex(txHash);
    }

    // ─── Nonce Query ─────────────────────────────────────────────────────

    private static long fetchNonce(String address) throws Exception {
        Map<String, String> params = new LinkedHashMap<>();
        params.put("address", address);
        String body = postForm(BASE_URL + "/query_account", params);
        // Parse JSON manually (avoid dependency)
        // Response: {"nonce":"5","balance":"1000",...}
        String nonceStr = extractJsonString(body, "nonce");
        if (nonceStr == null || nonceStr.isEmpty()) return 0;
        try {
            return Long.parseLong(nonceStr);
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    // ─── Receipt Polling ─────────────────────────────────────────────────

    private static Map<String, Object> waitReceipt(String txHash) throws Exception {
        long deadline = System.currentTimeMillis() + RECEIPT_TIMEOUT_SEC * 1000L;
        int notExistsCount = 0;
        while (System.currentTimeMillis() < deadline) {
            Map<String, String> params = new LinkedHashMap<>();
            params.put("tx_hash", txHash);
            String body = postForm(BASE_URL + "/transaction_receipt", params);

            int status = extractJsonInt(body, "status", -1);
            // kNotExists (100010): tx not yet in pool
            if (status == 100010) {
                notExistsCount++;
                if (notExistsCount >= 10) {
                    System.out.println("[receipt] tx not found after 10 retries");
                    return null;
                }
                Thread.sleep(1000);
                continue;
            }
            // kMessageHandle (10001) / kTxAccept (10003): still pending
            if (status == 10001 || status == 10003) {
                notExistsCount = 0;
                Thread.sleep(1000);
                continue;
            }
            // Final result
            Map<String, Object> result = new HashMap<>();
            result.put("status", status);
            result.put("raw", body);
            return result;
        }
        return null;
    }

    // ─── CREATE2 Address ─────────────────────────────────────────────────

    /**
     * CREATE2: keccak256(0xff + sender_20bytes + salt_32bytes + keccak256(bytecode))[-20:]
     * Salt: if not valid hex, hash the string to get 32 bytes.
     */
    private static String calcCreate2Address(String sender, String salt, String bytecode) {
        byte[] senderBytes = hexToBytes(sender.replace("0x", "").toLowerCase());

        // Salt → 32 bytes
        byte[] saltBytes;
        try {
            byte[] raw = hexToBytes(salt.replace("0x", "").toLowerCase());
            saltBytes = new byte[32];
            System.arraycopy(raw, 0, saltBytes, 32 - raw.length, raw.length);
        } catch (Exception e) {
            // Not hex — hash the string
            saltBytes = keccak256(salt.getBytes(StandardCharsets.UTF_8));
        }

        byte[] codeHash = keccak256(hexToBytes(bytecode.toLowerCase()));

        ByteArrayOutputStream buf = new ByteArrayOutputStream();
        buf.write(0xff);
        try {
            buf.write(senderBytes);
            buf.write(saltBytes);
            buf.write(codeHash);
        } catch (IOException e) {
            throw new RuntimeException(e);
        }

        byte[] hash = keccak256(buf.toByteArray());
        byte[] addr = new byte[20];
        System.arraycopy(hash, 12, addr, 0, 20);
        return bytesToHex(addr).toLowerCase();
    }

    // ─── Address Derivation ──────────────────────────────────────────────

    /** Derive Shardora address: keccak256(pubkey_64bytes)[-20:] */
    private static String deriveAddress(byte[] pubBytes64) {
        byte[] hash = keccak256(pubBytes64);
        byte[] addr = new byte[20];
        System.arraycopy(hash, 12, addr, 0, 20);
        return bytesToHex(addr).toLowerCase();
    }

    /** Get 64-byte uncompressed public key (no 0x04 prefix) */
    private static byte[] getPubKeyUncompressed(ECKeyPair keyPair) {
        byte[] full = Numeric.toBytesPadded(keyPair.getPublicKey(), 64);
        return full;
    }

    private static String toChecksumAddr(String addr) {
        // Shardora uses lowercase 40-char hex addresses
        return addr.toLowerCase().replace("0x", "");
    }

    // ─── Crypto Helpers ──────────────────────────────────────────────────

    private static byte[] keccak256(byte[] input) {
        Keccak.Digest256 digest = new Keccak.Digest256();
        return digest.digest(input);
    }

    // ─── Encoding Helpers ────────────────────────────────────────────────

    private static void writeLe64(OutputStream out, long value) throws IOException {
        byte[] buf = new byte[8];
        ByteBuffer.wrap(buf).order(ByteOrder.LITTLE_ENDIAN).putLong(value);
        out.write(buf);
    }

    private static byte[] hexToBytes(String hex) {
        hex = hex.replace("0x", "");
        if (hex.length() % 2 != 0) hex = "0" + hex;
        byte[] result = new byte[hex.length() / 2];
        for (int i = 0; i < result.length; i++) {
            result[i] = (byte) Integer.parseInt(hex.substring(i * 2, i * 2 + 2), 16);
        }
        return result;
    }

    private static String bytesToHex(byte[] bytes) {
        StringBuilder sb = new StringBuilder();
        for (byte b : bytes) sb.append(String.format("%02x", b & 0xff));
        return sb.toString();
    }

    // ─── HTTP Helpers ────────────────────────────────────────────────────

    private static String postForm(String urlStr, Map<String, String> params) throws Exception {
        StringBuilder formData = new StringBuilder();
        for (Map.Entry<String, String> e : params.entrySet()) {
            if (formData.length() > 0) formData.append('&');
            formData.append(URLEncoder.encode(e.getKey(), "UTF-8"))
                    .append('=')
                    .append(URLEncoder.encode(e.getValue(), "UTF-8"));
        }

        URL url = new URL(urlStr);
        HttpURLConnection conn = (HttpURLConnection) url.openConnection();
        conn.setRequestMethod("POST");
        conn.setDoOutput(true);
        conn.setConnectTimeout(30_000);
        conn.setReadTimeout(120_000);
        conn.setRequestProperty("Content-Type", "application/x-www-form-urlencoded");

        try (OutputStream os = conn.getOutputStream()) {
            os.write(formData.toString().getBytes(StandardCharsets.UTF_8));
        }

        int code = conn.getResponseCode();
        InputStream is = (code >= 200 && code < 300) ? conn.getInputStream() : conn.getErrorStream();
        if (is == null) return "";
        try (BufferedReader br = new BufferedReader(new InputStreamReader(is, StandardCharsets.UTF_8))) {
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = br.readLine()) != null) sb.append(line);
            return sb.toString();
        }
    }

    /** Minimal JSON string extractor — avoids adding a JSON library dependency */
    private static String extractJsonString(String json, String key) {
        String search = "\"" + key + "\"";
        int idx = json.indexOf(search);
        if (idx < 0) return null;
        idx = json.indexOf(':', idx + search.length());
        if (idx < 0) return null;
        idx++;
        while (idx < json.length() && json.charAt(idx) == ' ') idx++;
        if (idx >= json.length()) return null;
        if (json.charAt(idx) == '"') {
            int end = json.indexOf('"', idx + 1);
            return end > idx ? json.substring(idx + 1, end) : null;
        }
        // Numeric value without quotes
        int end = idx;
        while (end < json.length() && json.charAt(end) != ',' && json.charAt(end) != '}') end++;
        return json.substring(idx, end).trim();
    }

    private static int extractJsonInt(String json, String key, int defaultVal) {
        String val = extractJsonString(json, key);
        if (val == null) return defaultVal;
        try { return Integer.parseInt(val); } catch (NumberFormatException e) { return defaultVal; }
    }

    private static int getInt(Map<String, Object> map, String key, int defaultVal) {
        Object v = map.get(key);
        if (v instanceof Integer) return (Integer) v;
        if (v instanceof Number) return ((Number) v).intValue();
        return defaultVal;
    }

    // ─── SSL ─────────────────────────────────────────────────────────────

    private static void disableSslVerification() throws Exception {
        TrustManager[] trustAll = new TrustManager[]{
                new X509TrustManager() {
                    public void checkClientTrusted(X509Certificate[] c, String a) {}
                    public void checkServerTrusted(X509Certificate[] c, String a) {}
                    public X509Certificate[] getAcceptedIssuers() { return new X509Certificate[0]; }
                }
        };
        SSLContext sc = SSLContext.getInstance("TLS");
        sc.init(null, trustAll, new SecureRandom());
        javax.net.ssl.HttpsURLConnection.setDefaultSSLSocketFactory(sc.getSocketFactory());
        javax.net.ssl.HttpsURLConnection.setDefaultHostnameVerifier((h, s) -> true);
    }
}
