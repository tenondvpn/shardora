package main

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"math/big"
	"net"
	"net/http"
	"sync"
	"time"
)

// PurchaseCredential represents the purchase credential from user
type PurchaseCredential struct {
	Address   string `json:"address"`    // Shardora address to receive coins (hex encoded)
	Timestamp int64  `json:"timestamp"`  // Unix timestamp
	Nonce     string `json:"nonce"`      // Random nonce to prevent replay
	Signature string `json:"signature"`  // ECDSA signature (hex encoded, r||s||v format)
	PublicKey string `json:"public_key"` // User's public key (hex encoded, uncompressed format)
}

// Response represents the API response
type Response struct {
	Success bool   `json:"success"`
	Message string `json:"message"`
	TxHash  string `json:"tx_hash,omitempty"`
}

// CredentialService manages credential verification and coin transfers
type CredentialService struct {
	usedCredentials map[string]bool // Map of credential hash -> used status
	mu              sync.RWMutex
	shardoraClient  *ShardoraClient
}

// NewCredentialService creates a new credential service
func NewCredentialService(shardoraNodeIP string, shardoraNodePort int, senderPrivateKey string) *CredentialService {
	return &CredentialService{
		usedCredentials: make(map[string]bool),
		shardoraClient:  NewShardoraClient(shardoraNodeIP, shardoraNodePort, senderPrivateKey),
	}
}

// GetCredentialHash computes a unique hash for the credential
func (cs *CredentialService) GetCredentialHash(cred *PurchaseCredential) string {
	data := fmt.Sprintf("%s:%d:%s:%s", cred.Address, cred.Timestamp, cred.Nonce, cred.PublicKey)
	hash := sha256.Sum256([]byte(data))
	return hex.EncodeToString(hash[:])
}

// IsCredentialUsed checks if a credential has been used
func (cs *CredentialService) IsCredentialUsed(credHash string) bool {
	cs.mu.RLock()
	defer cs.mu.RUnlock()
	return cs.usedCredentials[credHash]
}

// MarkCredentialUsed marks a credential as used
func (cs *CredentialService) MarkCredentialUsed(credHash string) {
	cs.mu.Lock()
	defer cs.mu.Unlock()
	cs.usedCredentials[credHash] = true
}

// VerifySignature verifies the ECDSA signature of the credential
func (cs *CredentialService) VerifySignature(cred *PurchaseCredential) (bool, error) {
	// Decode public key (uncompressed format: 04 + X + Y, 65 bytes total)
	pubKeyBytes, err := hex.DecodeString(cred.PublicKey)
	if err != nil {
		return false, fmt.Errorf("invalid public key hex: %v", err)
	}

	if len(pubKeyBytes) != 65 || pubKeyBytes[0] != 0x04 {
		return false, fmt.Errorf("invalid public key format, expected uncompressed format (65 bytes starting with 0x04)")
	}

	// Parse public key
	x := new(big.Int).SetBytes(pubKeyBytes[1:33])
	y := new(big.Int).SetBytes(pubKeyBytes[33:65])
	pubKey := &ecdsa.PublicKey{
		Curve: elliptic.P256(),
		X:     x,
		Y:     y,
	}

	// Construct message to verify
	message := fmt.Sprintf("%s:%d:%s", cred.Address, cred.Timestamp, cred.Nonce)
	messageHash := sha256.Sum256([]byte(message))

	// Decode signature (r||s format, 64 bytes)
	sigBytes, err := hex.DecodeString(cred.Signature)
	if err != nil {
		return false, fmt.Errorf("invalid signature hex: %v", err)
	}

	if len(sigBytes) < 64 {
		return false, fmt.Errorf("invalid signature length: expected at least 64 bytes, got %d", len(sigBytes))
	}

	r := new(big.Int).SetBytes(sigBytes[0:32])
	s := new(big.Int).SetBytes(sigBytes[32:64])

	// Verify signature
	valid := ecdsa.Verify(pubKey, messageHash[:], r, s)
	return valid, nil
}

// ProcessCredential processes a purchase credential
func (cs *CredentialService) ProcessCredential(cred *PurchaseCredential) (*Response, error) {
	// 1. Validate timestamp (within 5 minutes)
	now := time.Now().Unix()
	if cred.Timestamp < now-300 || cred.Timestamp > now+300 {
		return &Response{
			Success: false,
			Message: "Credential timestamp is invalid or expired",
		}, nil
	}

	// 2. Check if credential already used
	credHash := cs.GetCredentialHash(cred)
	if cs.IsCredentialUsed(credHash) {
		return &Response{
			Success: false,
			Message: "Credential has already been used",
		}, nil
	}

	// 3. Verify signature
	valid, err := cs.VerifySignature(cred)
	if err != nil {
		return &Response{
			Success: false,
			Message: fmt.Sprintf("Signature verification failed: %v", err),
		}, nil
	}

	if !valid {
		return &Response{
			Success: false,
			Message: "Invalid signature",
		}, nil
	}

	// 4. Transfer coins via Shardora client
	txHash, err := cs.shardoraClient.TransferCoins(cred.Address, 10000000000)
	if err != nil {
		return &Response{
			Success: false,
			Message: fmt.Sprintf("Failed to transfer coins: %v", err),
		}, nil
	}

	// 5. Mark credential as used
	cs.MarkCredentialUsed(credHash)

	return &Response{
		Success: true,
		Message: "Coins transferred successfully",
		TxHash:  txHash,
	}, nil
}

// HandlePurchase handles the purchase endpoint
func (cs *CredentialService) HandlePurchase(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	// Parse request body
	body, err := ioutil.ReadAll(r.Body)
	if err != nil {
		http.Error(w, "Failed to read request body", http.StatusBadRequest)
		return
	}
	defer r.Body.Close()

	var cred PurchaseCredential
	if err := json.Unmarshal(body, &cred); err != nil {
		http.Error(w, "Invalid JSON format", http.StatusBadRequest)
		return
	}

	// Process credential
	resp, err := cs.ProcessCredential(&cred)
	if err != nil {
		log.Printf("Error processing credential: %v", err)
		http.Error(w, "Internal server error", http.StatusInternalServerError)
		return
	}

	// Send response
	w.Header().Set("Content-Type", "application/json")
	if resp.Success {
		w.WriteHeader(http.StatusOK)
	} else {
		w.WriteHeader(http.StatusBadRequest)
	}
	json.NewEncoder(w).Encode(resp)

	log.Printf("Processed credential for address %s: success=%v, message=%s",
		cred.Address, resp.Success, resp.Message)
}

// GenerateSelfSignedCert generates a self-signed TLS certificate
func GenerateSelfSignedCert() (tls.Certificate, error) {
	// Generate private key
	priv, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return tls.Certificate{}, err
	}

	// Create certificate template
	notBefore := time.Now()
	notAfter := notBefore.Add(365 * 24 * time.Hour) // Valid for 1 year

	serialNumber, err := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	if err != nil {
		return tls.Certificate{}, err
	}

	template := x509.Certificate{
		SerialNumber: serialNumber,
		Subject: pkix.Name{
			Organization: []string{"Shardora Purchase Service"},
			CommonName:   "localhost",
		},
		NotBefore:             notBefore,
		NotAfter:              notAfter,
		KeyUsage:              x509.KeyUsageKeyEncipherment | x509.KeyUsageDigitalSignature,
		ExtKeyUsage:           []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		BasicConstraintsValid: true,
		DNSNames:              []string{"localhost"},
		IPAddresses:           []net.IP{net.ParseIP("127.0.0.1")},
	}

	// Create certificate
	certDER, err := x509.CreateCertificate(rand.Reader, &template, &template, &priv.PublicKey, priv)
	if err != nil {
		return tls.Certificate{}, err
	}

	// Encode private key
	privBytes, err := x509.MarshalECPrivateKey(priv)
	if err != nil {
		return tls.Certificate{}, err
	}

	// Create TLS certificate
	cert := tls.Certificate{
		Certificate: [][]byte{certDER},
		PrivateKey:  priv,
	}

	log.Println("Generated self-signed certificate")
	return cert, nil
}

func main() {
	// Configuration
	port := 8443
	shardoraNodeIP := "127.0.0.1"
	shardoraNodePort := 13001
	senderPrivateKey := "your_sender_private_key_hex" // Replace with actual private key

	// Create credential service
	service := NewCredentialService(shardoraNodeIP, shardoraNodePort, senderPrivateKey)

	// Setup HTTP handlers
	http.HandleFunc("/purchase", service.HandlePurchase)
	http.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		w.Write([]byte("OK"))
	})

	// Generate self-signed certificate
	cert, err := GenerateSelfSignedCert()
	if err != nil {
		log.Fatalf("Failed to generate certificate: %v", err)
	}

	// Configure TLS
	tlsConfig := &tls.Config{
		Certificates: []tls.Certificate{cert},
		MinVersion:   tls.VersionTLS12,
	}

	// Create HTTPS server
	server := &http.Server{
		Addr:      fmt.Sprintf(":%d", port),
		TLSConfig: tlsConfig,
		Handler:   http.DefaultServeMux,
	}

	log.Printf("Starting HTTPS server on port %d...", port)
	log.Printf("Shardora node: %s:%d", shardoraNodeIP, shardoraNodePort)
	log.Printf("Endpoint: https://localhost:%d/purchase", port)

	// Start server
	if err := server.ListenAndServeTLS("", ""); err != nil {
		log.Fatalf("Server failed: %v", err)
	}
}
