import org.bouncycastle.jcajce.provider.digest.Keccak;
import org.web3j.abi.FunctionEncoder;
import org.web3j.abi.FunctionReturnDecoder;
import org.web3j.abi.TypeReference;
import org.web3j.abi.datatypes.Function;
import org.web3j.abi.datatypes.Type;
import org.web3j.abi.datatypes.Utf8String;
import org.web3j.crypto.ECDSASignature;
import org.web3j.crypto.ECKeyPair;
import org.web3j.utils.Numeric;

import javax.net.ssl.*;
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
 * TestKV — Shardora StringKeyValueDatabase Contract Tester
 * =========================================================
 * Tests set() and get() on an already-deployed contract.
 *
 * Protocol (matches shardora_sdk.py send_transaction_auto):
 *   Tx hash preimage: nonce(LE8) + pubkey(64B) + to(20B) + amount(LE8)
 *                     + gas_limit(LE8) + gas_price(LE8) + step(LE8)
 *                     + [input bytes] [+ prefund(LE8)]
 *   Nonce address for kContractExcute = contractAddr + senderAddr (concatenated hex)
 *
 * Usage:
 *   java -cp ".:lib/*" TestKV \
 *     --host 127.0.0.1 --port 23001 \
 *     --contract 38cfa809c88ae11408e3113daaf400274a48df6a \
 *     --key c8ee398141fe31308fce258fa4c0fc21288a74a221982db252cb10a94bf7063b
 */
public class TestKV {

    // Shardora step types (from shardora_sdk.py StepType)
    private static final int STEP_NORMAL_FROM          = 0;
    private static final int STEP_CONTRACT_GAS_PREFUND = 7;
    private static final int STEP_CONTRACT_EXECUTE     = 8;
    private static final int STEP_CONTRACT_REFUND      = 18;

    private static final long DEFAULT_GAS_LIMIT    = 5_000_000L;
    private static final long DEFAULT_GAS_PRICE    = 1L;
    private static final long DEFAULT_CALL_PREFUND = 1_000_000L;  // per-call gas deposit
    private static final int  RECEIPT_TIMEOUT_SEC  = 120;
    private static final int  NOT_EXISTS_MAX_RETRY = 10;

    // Defaults (can be overridden by --args)
    private static String host           = "121.40.200.145";
    private static int    port           = 23001;
    private static String contractAddr   = "38cfa809c88ae11408e3113daaf400274a48df6a";
    private static String privateKeyHex  = "c8ee398141fe31308fce258fa4c0fc21288a74a221982db252cb10a94bf7063b";

    private static String baseUrl;

    // ─── Entry Point ─────────────────────────────────────────────────────

    public static void main(String[] args) throws Exception {
        parseArgs(args);
        baseUrl = "https://" + host + ":" + port;
        disableSslVerification();

        // Derive public key and sender address from private key
        ECKeyPair keyPair = ECKeyPair.create(new BigInteger(privateKeyHex, 16));
        // Python SDK uses to_string("uncompressed") which is 04 || X || Y (65 bytes, 130 hex chars)
        String pubHex = "04" + Numeric.toHexStringNoPrefixZeroPadded(keyPair.getPublicKey(), 128);
        // Address derivation uses only the 64-byte X||Y portion (drop the 04 prefix)
        String senderAddr = deriveAddress(hexToBytes(pubHex.substring(2)));

        System.out.println("=== TestKV ===");
        System.out.printf("Host      : %s:%d%n", host, port);
        System.out.printf("Contract  : %s%n", contractAddr);
        System.out.printf("Sender    : %s%n", senderAddr);
        System.out.println("==============\n");

        runTests(keyPair, pubHex, senderAddr);
    }

    // ─── Test Cases ───────────────────────────────────────────────────────

    private static void runTests(ECKeyPair keyPair, String pubHex, String senderAddr) throws Exception {
        String[][] cases = {
            {"hello",        "world"},
            {"node.id",      "shard-03-node-001"},
            {"config.debug", "false"},
        };

        int pass = 0, fail = 0;

        for (String[] tc : cases) {
            String key   = tc[0];
            String value = tc[1];

            System.out.printf("--- set(\"%s\", \"%s\") ---%n", key, value);

            // 1. set(key, value)
            boolean setOk = callSet(keyPair, pubHex, senderAddr, key, value);

            // 2. get(key)
            String got = callGet(senderAddr, key);
            boolean match = value.equals(got);

            System.out.printf("    set: %s | get -> \"%s\" | %s%n",
                    setOk ? "OK" : "FAILED", got, match ? "PASS ✓" : "FAIL ✗");

            if (match) pass++; else fail++;
            System.out.println();
        }

        System.out.printf("=== Results: %d/%d passed ===%n", pass, pass + fail);
    }

    // ─── Contract Methods ─────────────────────────────────────────────────

    /** Call set(key, value) — state-changing transaction */
    private static boolean callSet(ECKeyPair keyPair, String pubHex, String senderAddr,
                                    String key, String value) throws Exception {
        String inputHex = encodeSet(key, value);

        // Nonce address for kContractExcute = contractAddr + senderAddr
        String nonceAddr = contractAddr + senderAddr;
        long nonce = fetchNonce(nonceAddr) + 1;

        String txHash = buildAndSend(keyPair, pubHex, nonce, contractAddr,
                0L, STEP_CONTRACT_EXECUTE, "", inputHex, DEFAULT_CALL_PREFUND);
        System.out.printf("    txHash: %s%n", txHash);

        Map<String, Object> receipt = waitReceipt(txHash);
        if (receipt == null) {
            System.out.println("    set() receipt: TIMEOUT");
            return false;
        }
        int status = (int) receipt.get("status");
        System.out.printf("    receipt: status=%d%n", status);
        return status == 0;
    }

    /** Call get(key) — read-only ABI query, no gas needed */
    private static String callGet(String senderAddr, String key) throws Exception {
        String inputHex = encodeGet(key);
        String raw = queryContract(senderAddr, contractAddr, inputHex);
        return decodeGetResult(raw);
    }

    // ─── ABI Encoding / Decoding ──────────────────────────────────────────

    @SuppressWarnings("unchecked")
    private static String encodeSet(String key, String value) {
        Function fn = new Function("set",
                Arrays.asList(new Utf8String(key), new Utf8String(value)),
                Collections.emptyList());
        return Numeric.cleanHexPrefix(FunctionEncoder.encode(fn));
    }

    @SuppressWarnings("unchecked")
    private static String encodeGet(String key) {
        Function fn = new Function("get",
                Arrays.asList(new Utf8String(key)),
                Arrays.asList(new TypeReference<Utf8String>() {}));
        return Numeric.cleanHexPrefix(FunctionEncoder.encode(fn));
    }

    @SuppressWarnings("unchecked")
    private static String decodeGetResult(String hexResult) {
        try {
            String clean = hexResult.replace("0x", "").trim();
            if (clean.isEmpty()) return "(empty)";
            Function fn = new Function("get",
                    Arrays.asList(new Utf8String("")),
                    Arrays.asList(new TypeReference<Utf8String>() {}));
            List<Type> decoded = FunctionReturnDecoder.decode(clean, fn.getOutputParameters());
            if (decoded.isEmpty()) return "(decode failed)";
            return ((Utf8String) decoded.get(0)).getValue();
        } catch (Exception e) {
            return "(decode error: " + e.getMessage() + ")";
        }
    }

    // ─── Query (read-only call) ───────────────────────────────────────────

    private static String queryContract(String from, String address, String inputHex) throws Exception {
        Map<String, String> params = new LinkedHashMap<>();
        params.put("from", from);
        params.put("address", address);
        params.put("input", inputHex);
        String resp = postForm(baseUrl + "/abi_query_contract", params);
        System.out.printf("    query_contract raw: %s%n",
                resp.length() > 100 ? resp.substring(0, 100) + "..." : resp);
        return resp;
    }

    // ─── Transaction Builder ─────────────────────────────────────────────

    /**
     * Build Shardora transaction message, sign, and POST to /transaction.
     *
     * Message format (little-endian uint64 for numeric fields):
     *   nonce(8) + pubkey(64) + to(20) + amount(8) + gasLimit(8) + gasPrice(8)
     *   + step(8) [+ contractCode] [+ input] [+ prefund(8)]
     */
    private static String buildAndSend(ECKeyPair keyPair, String pubHex,
                                        long nonce, String toAddr, long amount,
                                        int step, String contractCode,
                                        String inputHex, long prefund)
            throws Exception {

        String cleanTo    = normalizeHex(toAddr);
        String cleanCode  = normalizeHex(contractCode);
        String cleanInput = normalizeHex(inputHex);

        ByteArrayOutputStream buf = new ByteArrayOutputStream();
        writeLe64(buf, nonce);
        buf.write(hexToBytes(pubHex));           // 65-byte uncompressed pubkey (04 || X || Y)
        buf.write(hexToBytes(cleanTo));           // 20-byte destination
        writeLe64(buf, amount);
        writeLe64(buf, DEFAULT_GAS_LIMIT);
        writeLe64(buf, DEFAULT_GAS_PRICE);
        writeLe64(buf, step);
        if (!cleanCode.isEmpty())  buf.write(hexToBytes(cleanCode));
        if (!cleanInput.isEmpty()) buf.write(hexToBytes(cleanInput));
        if (prefund > 0)           writeLe64(buf, prefund);

        byte[] txHash = keccak256(buf.toByteArray());

        // Deterministic ECDSA (RFC 6979, SHA-256 HMAC), canonical low-S
        // secp256k1 curve order N (hardcoded — matches Python sigencode_string_canonize)
        final BigInteger SECP256K1_N = new BigInteger(
                "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141", 16);
        ECDSASignature sig = keyPair.sign(txHash);
        if (sig.s.compareTo(SECP256K1_N.shiftRight(1)) > 0) {
            sig = new ECDSASignature(sig.r, SECP256K1_N.subtract(sig.s));
        }

        Map<String, String> params = new LinkedHashMap<>();
        params.put("nonce",     String.valueOf(nonce));
        params.put("pubkey",    pubHex);
        params.put("to",        cleanTo);
        params.put("amount",    String.valueOf(amount));
        params.put("gas_limit", String.valueOf(DEFAULT_GAS_LIMIT));
        params.put("gas_price", String.valueOf(DEFAULT_GAS_PRICE));
        params.put("shard_id",  "0");
        params.put("type",      String.valueOf(step));
        params.put("sign_r",    Numeric.toHexStringNoPrefixZeroPadded(sig.r, 64));
        params.put("sign_s",    Numeric.toHexStringNoPrefixZeroPadded(sig.s, 64));
        params.put("sign_v",    "0");
        if (!cleanCode.isEmpty())  params.put("bytes_code", cleanCode);
        if (!cleanInput.isEmpty()) params.put("input",      cleanInput);
        if (prefund > 0)           params.put("prefund",    String.valueOf(prefund));

        String resp = postForm(baseUrl + "/transaction", params);
        System.out.printf("    [send_tx] %s%n",
                resp.length() > 120 ? resp.substring(0, 120) : resp);

        // Auto-retry on nonce conflict
        if (resp.contains("kTxUserNonceInvalid")) {
            System.out.println("    [nonce_invalid] retrying...");
            long oldNonce = nonce - 1;
            for (int i = 0; i < 30; i++) {
                Thread.sleep(1000);
                // pubHex is "04"+X+Y (65 bytes); deriveAddress wants the 64-byte X||Y portion
                byte[] pubBytes64 = hexToBytes(pubHex.substring(2));
                String nonceAddr = (step == STEP_CONTRACT_EXECUTE || step == STEP_CONTRACT_REFUND)
                        ? cleanTo + deriveAddress(pubBytes64)
                        : deriveAddress(pubBytes64);
                long cur = fetchNonce(nonceAddr);
                if (cur != oldNonce) {
                    return buildAndSend(keyPair, pubHex, cur + 1, toAddr,
                            amount, step, contractCode, inputHex, prefund);
                }
            }
        }

        return bytesToHex(txHash);
    }

    // ─── Prefund (optional helper) ────────────────────────────────────────

    /**
     * Deposit gas into the contract's prefund pool for this sender.
     * Call this once if the account has no prefund balance.
     * nonce address for kContractGasPrefund = senderAddr (not contractAddr+sender)
     */
    public static void prefundContract(ECKeyPair keyPair, String pubHex,
                                        String senderAddr, long amount) throws Exception {
        System.out.printf("[prefund] depositing %d for %s...%n", amount, senderAddr);
        long nonce = fetchNonce(senderAddr) + 1;
        String txHash = buildAndSend(keyPair, pubHex, nonce, contractAddr,
                0L, STEP_CONTRACT_GAS_PREFUND, "", "", amount);
        System.out.printf("[prefund] txHash=%s%n", txHash);
        Map<String, Object> receipt = waitReceipt(txHash);
        System.out.printf("[prefund] status=%s%n",
                receipt != null ? receipt.get("status") : "timeout");
    }

    // ─── Nonce & Receipt ─────────────────────────────────────────────────

    private static long fetchNonce(String address) throws Exception {
        Map<String, String> params = new LinkedHashMap<>();
        params.put("address", normalizeHex(address));
        String body = postForm(baseUrl + "/query_account", params);
        String val = extractJsonString(body, "nonce");
        if (val == null || val.isEmpty()) return 0;
        try { return Long.parseLong(val); } catch (NumberFormatException e) { return 0; }
    }

    private static Map<String, Object> waitReceipt(String txHash) throws Exception {
        long deadline = System.currentTimeMillis() + RECEIPT_TIMEOUT_SEC * 1000L;
        int notExistsCount = 0;
        while (System.currentTimeMillis() < deadline) {
            Map<String, String> params = new LinkedHashMap<>();
            params.put("tx_hash", txHash);
            String body = postForm(baseUrl + "/transaction_receipt", params);
            int status = extractJsonInt(body, "status", -99);
            System.out.printf("    [receipt] status=%d%n", status);

            if (status == 100010) {      // kNotExists — tx not yet in pool
                if (++notExistsCount >= NOT_EXISTS_MAX_RETRY) return null;
                Thread.sleep(1000);
                continue;
            }
            if (status == 10001 || status == 10003) {   // kMessageHandle / kTxAccept — pending
                notExistsCount = 0;
                Thread.sleep(1000);
                continue;
            }
            if (status == -99) {         // parse error
                Thread.sleep(1000);
                continue;
            }
            Map<String, Object> result = new HashMap<>();
            result.put("status", status);
            result.put("raw",    body);
            return result;
        }
        System.out.printf("    [receipt] timeout after %ds%n", RECEIPT_TIMEOUT_SEC);
        return null;
    }

    // ─── Address Derivation ───────────────────────────────────────────────

    /** keccak256(64-byte pubkey)[-20:] — matches ShardoraClient.get_address() */
    private static String deriveAddress(byte[] pubBytes64) {
        byte[] hash = keccak256(pubBytes64);
        return bytesToHex(Arrays.copyOfRange(hash, 12, 32));
    }

    // ─── Crypto ──────────────────────────────────────────────────────────

    private static byte[] keccak256(byte[] input) {
        return new Keccak.Digest256().digest(input);
    }

    // ─── Byte / Hex Utilities ─────────────────────────────────────────────

    private static String normalizeHex(String hex) {
        if (hex == null || hex.isEmpty()) return "";
        hex = hex.replace("0x", "").toLowerCase().trim();
        if (hex.length() % 2 != 0) hex = "0" + hex;
        return hex;
    }

    private static byte[] hexToBytes(String hex) {
        hex = normalizeHex(hex);
        if (hex.isEmpty()) return new byte[0];
        byte[] out = new byte[hex.length() / 2];
        for (int i = 0; i < out.length; i++)
            out[i] = (byte) Integer.parseInt(hex.substring(i * 2, i * 2 + 2), 16);
        return out;
    }

    private static String bytesToHex(byte[] bytes) {
        StringBuilder sb = new StringBuilder(bytes.length * 2);
        for (byte b : bytes) sb.append(String.format("%02x", b & 0xff));
        return sb.toString();
    }

    private static void writeLe64(OutputStream out, long value) throws IOException {
        byte[] buf = new byte[8];
        ByteBuffer.wrap(buf).order(ByteOrder.LITTLE_ENDIAN).putLong(value);
        out.write(buf);
    }

    // ─── HTTP ────────────────────────────────────────────────────────────

    private static String postForm(String urlStr, Map<String, String> params) throws Exception {
        StringBuilder form = new StringBuilder();
        for (Map.Entry<String, String> e : params.entrySet()) {
            if (form.length() > 0) form.append('&');
            form.append(URLEncoder.encode(e.getKey(), "UTF-8"))
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
            os.write(form.toString().getBytes(StandardCharsets.UTF_8));
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

    // ─── Minimal JSON Extraction ──────────────────────────────────────────

    private static String extractJsonString(String json, String key) {
        String search = "\"" + key + "\"";
        int idx = json.indexOf(search);
        if (idx < 0) return null;
        idx = json.indexOf(':', idx + search.length());
        if (idx < 0) return null;
        while (++idx < json.length() && json.charAt(idx) == ' ');
        if (idx >= json.length()) return null;
        if (json.charAt(idx) == '"') {
            int end = json.indexOf('"', idx + 1);
            return end > idx ? json.substring(idx + 1, end) : null;
        }
        int end = idx;
        while (end < json.length() && json.charAt(end) != ',' && json.charAt(end) != '}') end++;
        return json.substring(idx, end).trim();
    }

    private static int extractJsonInt(String json, String key, int def) {
        String val = extractJsonString(json, key);
        if (val == null) return def;
        try { return Integer.parseInt(val.trim()); } catch (NumberFormatException e) { return def; }
    }

    // ─── SSL Bypass (self-signed certs) ──────────────────────────────────

    private static void disableSslVerification() throws Exception {
        TrustManager[] ta = {new X509TrustManager() {
            public void checkClientTrusted(X509Certificate[] c, String a) {}
            public void checkServerTrusted(X509Certificate[] c, String a) {}
            public X509Certificate[] getAcceptedIssuers() { return new X509Certificate[0]; }
        }};
        SSLContext sc = SSLContext.getInstance("TLS");
        sc.init(null, ta, new SecureRandom());
        HttpsURLConnection.setDefaultSSLSocketFactory(sc.getSocketFactory());
        HttpsURLConnection.setDefaultHostnameVerifier((h, s) -> true);
    }

    // ─── Argument Parsing ────────────────────────────────────────────────

    private static void parseArgs(String[] args) {
        for (int i = 0; i + 1 < args.length; i++) {
            switch (args[i]) {
                case "--host":     host          = args[++i]; break;
                case "--port":     port          = Integer.parseInt(args[++i]); break;
                case "--contract": contractAddr  = args[++i]; break;
                case "--key":      privateKeyHex = args[++i]; break;
            }
        }
    }
}
