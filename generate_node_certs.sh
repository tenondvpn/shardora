#!/bin/bash

# Script to generate SSL certificates for existing nodes

NODES_DIR="/root/shardoras"
LOCAL_IP=$(hostname -I | awk '{print $1}')

if [ -z "$LOCAL_IP" ]; then
    LOCAL_IP="127.0.0.1"
fi

echo "Generating SSL certificates for all nodes in $NODES_DIR"
echo "Using local IP: $LOCAL_IP"
echo ""

# Check if openssl is available
if ! command -v openssl &> /dev/null; then
    echo "Error: openssl is not installed"
    echo "Please install openssl: apt-get install openssl"
    exit 1
fi

# Counter for generated certificates
count=0

# Find all node directories
for node_dir in $NODES_DIR/s*; do
    if [ -d "$node_dir" ]; then
        node_name=$(basename "$node_dir")
        
        # Check if certificates already exist
        if [ -f "$node_dir/server-cert.pem" ] && [ -f "$node_dir/server-key.pem" ]; then
            echo "Certificates already exist for $node_name, skipping..."
            continue
        fi
        
        echo "Generating certificate for $node_name..."
        
        # Generate self-signed certificate
        openssl req -x509 -newkey rsa:2048 -nodes \
            -keyout "$node_dir/server-key.pem" \
            -out "$node_dir/server-cert.pem" \
            -days 365 \
            -subj "/C=CN/ST=State/L=City/O=Shardora/OU=Node/CN=$LOCAL_IP" \
            2>/dev/null
        
        if [ $? -eq 0 ]; then
            # Set proper permissions
            chmod 600 "$node_dir/server-key.pem"
            chmod 644 "$node_dir/server-cert.pem"
            echo "  ✓ Certificate generated: $node_dir/server-cert.pem"
            echo "  ✓ Private key generated: $node_dir/server-key.pem"
            ((count++))
        else
            echo "  ✗ Failed to generate certificate for $node_name"
        fi
        echo ""
    fi
done

echo "=========================================="
echo "Certificate generation complete!"
echo "Generated certificates for $count nodes"
echo "=========================================="
echo ""
echo "Certificate details:"
echo "  - Algorithm: RSA 2048-bit"
echo "  - Validity: 365 days"
echo "  - Common Name: $LOCAL_IP"
echo ""
echo "To verify a certificate:"
echo "  openssl x509 -in /root/shardoras/s3_1/server-cert.pem -text -noout"
echo ""
echo "To restart nodes with HTTPS:"
echo "  killall -9 shardora"
echo "  # Then start your nodes normally"
