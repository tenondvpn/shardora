package main

import (
	"bytes"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/tls"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net/http"
	"time"
)

// ClientExample demonstrates how to create and submit a purchase credential
type ClientExample struct {
	privateKey *ecdsa.PrivateKey
	publicKey  string
	serverURL  string
}

// NewClientExample creates a new client example
func NewClientExample(serverURL string) (*ClientExample, error) {
	// Generate ECDSA key pair
	privKey, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return nil, fmt.Errorf("failed to generate key: %v", err)
	}

	// Encode public key (uncompressed format)
	pubKeyBytes := make([]byte, 65)
	pubKeyBytes[0] = 0x04
	copy(pubKeyBytes[1:33], privKey.PublicKey.X.Bytes())
	copy(pubKeyBytes[33:65], privKey.PublicKey.Y.Bytes())

	return &ClientExample{
		privateKey: privKey,
		publicKey:  hex.EncodeToString(pubKeyBytes),
		serverURL:  serverURL,
	}, nil
}

// CreateCredential creates a signed purchase credential
func (ce *ClientExample) CreateCredential(shardoraAddress string) (*PurchaseCredential, error) {
	// Generate nonce
	nonceBytes := make([]byte, 16)
	if _, err := rand.Read(nonceBytes); err != nil {
		return nil, fmt.Errorf("failed to generate nonce: %v", err)
	}
	nonce := hex.EncodeToString(nonceBytes)

	// Create credential
	timestamp := time.Now().Unix()
	cred := &PurchaseCredential{
		Address:   shardoraAddress,
		Timestamp: timestamp,
		Nonce:     nonce,
		PublicKey: ce.publicKey,
	}

	// Sign credential
	message := fmt.Sprintf("%s:%d:%s", cred.Address, cred.Timestamp, cred.Nonce)
	messageHash := sha256.Sum256([]byte(message))

	r, s, err := ecdsa.Sign(rand.Reader, ce.privateKey, messageHash[:])
	if err != nil {
		return nil, fmt.Errorf("failed to sign: %v", err)
	}

	// Encode signature (r||s format, 64 bytes)
	signature := make([]byte, 64)
	rBytes := r.Bytes()
	sBytes := s.Bytes()
	copy(signature[32-len(rBytes):32], rBytes)
	copy(signature[64-len(sBytes):64], sBytes)

	cred.Signature = hex.EncodeToString(signature)

	return cred, nil
}

// SubmitCredential submits the credential to the server
func (ce *ClientExample) SubmitCredential(cred *PurchaseCredential) (*Response, error) {
	// Marshal credential to JSON
	credJSON, err := json.Marshal(cred)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal credential: %v", err)
	}

	// Create HTTP client that accepts self-signed certificates
	tr := &http.Transport{
		TLSClientConfig: &tls.Config{InsecureSkipVerify: true},
	}
	client := &http.Client{Transport: tr}

	// Send POST request
	resp, err := client.Post(ce.serverURL+"/purchase", "application/json", bytes.NewBuffer(credJSON))
	if err != nil {
		return nil, fmt.Errorf("failed to send request: %v", err)
	}
	defer resp.Body.Close()

	// Read response
	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("failed to read response: %v", err)
	}

	// Parse response
	var response Response
	if err := json.Unmarshal(body, &response); err != nil {
		return nil, fmt.Errorf("failed to parse response: %v", err)
	}

	return &response, nil
}

// Example usage (commented out to avoid conflicts with main)
/*
func main() {
	// Create client
	client, err := NewClientExample("https://localhost:8443")
	if err != nil {
		fmt.Printf("Failed to create client: %v\n", err)
		return
	}

	// Shardora address to receive coins (hex encoded)
	shardoraAddress := "1234567890abcdef1234567890abcdef12345678"

	// Create credential
	cred, err := client.CreateCredential(shardoraAddress)
	if err != nil {
		fmt.Printf("Failed to create credential: %v\n", err)
		return
	}

	fmt.Printf("Created credential:\n")
	fmt.Printf("  Address: %s\n", cred.Address)
	fmt.Printf("  Timestamp: %d\n", cred.Timestamp)
	fmt.Printf("  Nonce: %s\n", cred.Nonce)
	fmt.Printf("  PublicKey: %s\n", cred.PublicKey)
	fmt.Printf("  Signature: %s\n", cred.Signature)

	// Submit credential
	resp, err := client.SubmitCredential(cred)
	if err != nil {
		fmt.Printf("Failed to submit credential: %v\n", err)
		return
	}

	fmt.Printf("\nServer response:\n")
	fmt.Printf("  Success: %v\n", resp.Success)
	fmt.Printf("  Message: %s\n", resp.Message)
	if resp.TxHash != "" {
		fmt.Printf("  TxHash: %s\n", resp.TxHash)
	}

	// Try to submit the same credential again (should fail)
	fmt.Printf("\nTrying to reuse credential...\n")
	resp2, err := client.SubmitCredential(cred)
	if err != nil {
		fmt.Printf("Failed to submit credential: %v\n", err)
		return
	}

	fmt.Printf("Server response:\n")
	fmt.Printf("  Success: %v\n", resp2.Success)
	fmt.Printf("  Message: %s\n", resp2.Message)
}
*/
