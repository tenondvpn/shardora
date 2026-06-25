package main

import (
	"bytes"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"math/big"
	"net/http"
)

// ShardoraClient handles communication with Shardora blockchain
type ShardoraClient struct {
	nodeIP       string
	nodePort     int
	httpPort     int
	privateKey   string
	publicKey    string
	address      string
	currentNonce uint64
	ecdsaPrivKey *ecdsa.PrivateKey
}

// ShardoraTransaction represents a Shardora transaction
type ShardoraTransaction struct {
	Nonce    uint64 `json:"nonce"`
	PubKey   string `json:"pubkey"`
	Step     int    `json:"step"`
	To       string `json:"to"`
	Amount   uint64 `json:"amount"`
	GasLimit uint64 `json:"gas_limit"`
	GasPrice uint64 `json:"gas_price"`
	Sign     string `json:"sign"`
}

// ShardoraAPIResponse represents Shardora API response
type ShardoraAPIResponse struct {
	Status int                    `json:"status"`
	Msg    string                 `json:"msg"`
	Data   map[string]interface{} `json:"data,omitempty"`
}

// NewShardoraClient creates a new Shardora client
func NewShardoraClient(nodeIP string, nodePort int, privateKeyHex string) *ShardoraClient {
	client := &ShardoraClient{
		nodeIP:   nodeIP,
		nodePort: nodePort,
		httpPort: nodePort + 10000, // HTTP port is typically TCP port + 10000
	}

	// Parse private key
	if err := client.SetPrivateKey(privateKeyHex); err != nil {
		panic(fmt.Sprintf("Failed to set private key: %v", err))
	}

	// Fetch initial nonce
	if err := client.UpdateNonce(); err != nil {
		panic(fmt.Sprintf("Failed to fetch initial nonce: %v", err))
	}

	return client
}

// SetPrivateKey sets the private key and derives public key and address
func (sc *ShardoraClient) SetPrivateKey(privateKeyHex string) error {
	// Decode private key
	privKeyBytes, err := hex.DecodeString(privateKeyHex)
	if err != nil {
		return fmt.Errorf("invalid private key hex: %v", err)
	}

	// Parse ECDSA private key (secp256k1 or P256)
	curve := elliptic.P256() // Use P256, adjust if Shardora uses secp256k1
	privKey := new(ecdsa.PrivateKey)
	privKey.PublicKey.Curve = curve
	privKey.D = new(big.Int).SetBytes(privKeyBytes)
	privKey.PublicKey.X, privKey.PublicKey.Y = curve.ScalarBaseMult(privKeyBytes)

	sc.ecdsaPrivKey = privKey
	sc.privateKey = privateKeyHex

	// Generate uncompressed public key (0x04 + X + Y)
	pubKeyBytes := make([]byte, 65)
	pubKeyBytes[0] = 0x04
	copy(pubKeyBytes[1:33], privKey.PublicKey.X.Bytes())
	copy(pubKeyBytes[33:65], privKey.PublicKey.Y.Bytes())
	sc.publicKey = hex.EncodeToString(pubKeyBytes)

	// Generate address (last 20 bytes of keccak256(pubkey))
	// For simplicity, using SHA256 here - adjust to keccak256 if needed
	hash := sha256.Sum256(pubKeyBytes[1:])     // Skip 0x04 prefix
	sc.address = hex.EncodeToString(hash[12:]) // Last 20 bytes

	return nil
}

// UpdateNonce fetches the current nonce from the blockchain
func (sc *ShardoraClient) UpdateNonce() error {
	url := fmt.Sprintf("http://%s:%d/api/account?address=%s", sc.nodeIP, sc.httpPort, sc.address)

	resp, err := http.Get(url)
	if err != nil {
		return fmt.Errorf("failed to fetch nonce: %v", err)
	}
	defer resp.Body.Close()

	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return fmt.Errorf("failed to read response: %v", err)
	}

	var apiResp ShardoraAPIResponse
	if err := json.Unmarshal(body, &apiResp); err != nil {
		return fmt.Errorf("failed to parse response: %v", err)
	}

	if apiResp.Status != 0 {
		return fmt.Errorf("API error: %s", apiResp.Msg)
	}

	// Extract nonce from response
	if data, ok := apiResp.Data["nonce"].(float64); ok {
		sc.currentNonce = uint64(data)
	} else {
		sc.currentNonce = 0
	}

	return nil
}

// SignTransaction signs a transaction
func (sc *ShardoraClient) SignTransaction(txHash []byte) (string, error) {
	// Sign with ECDSA
	r, s, err := ecdsa.Sign(bytes.NewReader(txHash), sc.ecdsaPrivKey, txHash)
	if err != nil {
		return "", fmt.Errorf("failed to sign: %v", err)
	}

	// Encode signature as r||s||v (65 bytes)
	signature := make([]byte, 65)
	copy(signature[0:32], r.Bytes())
	copy(signature[32:64], s.Bytes())
	signature[64] = 0 // Recovery ID (v), set to 0 for simplicity

	return hex.EncodeToString(signature), nil
}

// GetTransactionHash computes the transaction hash
func (sc *ShardoraClient) GetTransactionHash(tx *ShardoraTransaction) []byte {
	// Construct transaction data for hashing
	data := fmt.Sprintf("%d:%s:%d:%s:%d:%d:%d",
		tx.Nonce, tx.PubKey, tx.Step, tx.To, tx.Amount, tx.GasLimit, tx.GasPrice)
	hash := sha256.Sum256([]byte(data))
	return hash[:]
}

// TransferCoins transfers coins to the specified address
func (sc *ShardoraClient) TransferCoins(toAddress string, amount uint64) (string, error) {
	// Increment nonce
	sc.currentNonce++

	// Decode to address
	toBytes, err := hex.DecodeString(toAddress)
	if err != nil {
		return "", fmt.Errorf("invalid to address: %v", err)
	}

	// Create transaction
	tx := &ShardoraTransaction{
		Nonce:    sc.currentNonce,
		PubKey:   sc.publicKey,
		Step:     0, // kNormalFrom
		To:       string(toBytes),
		Amount:   amount,
		GasLimit: 1000,
		GasPrice: 1,
	}

	// Compute transaction hash
	txHash := sc.GetTransactionHash(tx)

	// Sign transaction
	signature, err := sc.SignTransaction(txHash)
	if err != nil {
		return "", fmt.Errorf("failed to sign transaction: %v", err)
	}
	tx.Sign = signature

	// Send transaction via HTTP API
	txJSON, err := json.Marshal(tx)
	if err != nil {
		return "", fmt.Errorf("failed to marshal transaction: %v", err)
	}

	url := fmt.Sprintf("http://%s:%d/api/transaction", sc.nodeIP, sc.httpPort)
	resp, err := http.Post(url, "application/json", bytes.NewBuffer(txJSON))
	if err != nil {
		return "", fmt.Errorf("failed to send transaction: %v", err)
	}
	defer resp.Body.Close()

	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return "", fmt.Errorf("failed to read response: %v", err)
	}

	var apiResp ShardoraAPIResponse
	if err := json.Unmarshal(body, &apiResp); err != nil {
		return "", fmt.Errorf("failed to parse response: %v", err)
	}

	if apiResp.Status != 0 {
		return "", fmt.Errorf("transaction failed: %s", apiResp.Msg)
	}

	// Return transaction hash
	txHashHex := hex.EncodeToString(txHash)
	return txHashHex, nil
}

// GetBalance retrieves the balance of an address
func (sc *ShardoraClient) GetBalance(address string) (uint64, error) {
	url := fmt.Sprintf("http://%s:%d/api/account?address=%s", sc.nodeIP, sc.httpPort, address)

	resp, err := http.Get(url)
	if err != nil {
		return 0, fmt.Errorf("failed to fetch balance: %v", err)
	}
	defer resp.Body.Close()

	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return 0, fmt.Errorf("failed to read response: %v", err)
	}

	var apiResp ShardoraAPIResponse
	if err := json.Unmarshal(body, &apiResp); err != nil {
		return 0, fmt.Errorf("failed to parse response: %v", err)
	}

	if apiResp.Status != 0 {
		return 0, fmt.Errorf("API error: %s", apiResp.Msg)
	}

	// Extract balance from response
	if data, ok := apiResp.Data["balance"].(float64); ok {
		return uint64(data), nil
	}

	return 0, nil
}
