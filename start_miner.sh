#!/bin/bash

# --- 1. Configuration & Variables ---
TARGET="Debug"
INSTALL_DIR="/root/shardora_miner"
rm -rf $INSTALL_DIR
cp -rf ./mining_node $INSTALL_DIR
CONFIG_FILE="$INSTALL_DIR/conf/shardora.conf"
CONFIG_TEMP="$INSTALL_DIR/conf/shardora.conf_temp"
SERVICE_NAME="shardora_miner"

# Check if argument 1 is provided
if [ -z "$1" ]; then
    echo "No private key provided. Generating a random 32-byte hex key..."
    # Generate 32 bytes of random data and convert to hex (64 characters)
    RAW_PRIVATE_KEY=$(openssl rand -hex 32)
else
    RAW_PRIVATE_KEY="$1"
fi

echo "Starting deployment for $SERVICE_NAME..."

# --- 2. Build and Environment Setup ---
bash build.sh a $TARGET
# --- 3. Network Discovery ---
if command -v hostname > /dev/null; then
    LOCAL_IP=$(hostname -I | awk '{print $1}')
else
    LOCAL_IP=$(ip addr show | grep -w inet | grep -v 127.0.0.1 | awk '{print $2}' | cut -d/ -f1 | head -n 1)
fi

PUBLIC_IP=$(curl -s --connect-timeout 5 ifconfig.me || curl -s --connect-timeout 5 ipinfo.io/ip)

if [ -z "$LOCAL_IP" ] || [ -z "$PUBLIC_IP" ]; then
    echo "Error: Failed to retrieve network IP addresses."
    exit 1
fi

# --- 4. Encrypt Private Key ---
ENCRYPT_CMD="./cbuild_$TARGET/shardora"
mkdir -p $INSTALL_DIR/bin $INSTALL_DIR/log
cp -rf $ENCRYPT_CMD $INSTALL_DIR/bin/shardora


OUTPUT=$($ENCRYPT_CMD -K "${RAW_PRIVATE_KEY}")
PRIVATE_KEY=""
if [ $? -eq 0 ] && [ -n "$OUTPUT" ]; then
    IFS=":" read -r PRIVATE_KEY WALLET_ADDRESS <<< "$OUTPUT"
else
    echo "Error: Failed to get key and address."
    exit 1
fi

if [ $? -ne 0 ]; then
    echo "Encryption failed! Details: $PRIVATE_KEY"
    exit 1
fi

# --- 5. Install Files & Update Config ---
cp "$ENCRYPT_CMD" "$INSTALL_DIR/bin/shardora"
if [ -f "$CONFIG_TEMP" ]; then
    cp -rf "$CONFIG_TEMP" "$CONFIG_FILE"
    sed -i "s@REPLACE_PRIVATE_KEY@$PRIVATE_KEY@g" "$CONFIG_FILE"
    sed -i "s@REPLACE_LOCAL_IP@$LOCAL_IP@g" "$CONFIG_FILE"
    sed -i "s@REPLACE_PUBLIC_IP@$PUBLIC_IP@g" "$CONFIG_FILE"
else
    echo "Warning: Template $CONFIG_TEMP not found. Skipping config update."
fi

# --- 6. Create Systemd Service ---
echo "Creating systemd service..."
cat <<EOF > /etc/systemd/system/$SERVICE_NAME.service
[Unit]
Description=Shardora Mining Node
After=network.target

[Service]
Type=simple
User=root
WorkingDirectory=$INSTALL_DIR
ExecStart=$INSTALL_DIR/bin/shardora -f 0 -g 0
Restart=always
RestartSec=5
StandardOutput=append:$INSTALL_DIR/log/stdout.log
StandardError=append:$INSTALL_DIR/log/stderr.log

[Install]
WantedBy=multi-user.target
EOF

# --- 7. Start Service ---
systemctl daemon-reload
systemctl enable $SERVICE_NAME
systemctl restart $SERVICE_NAME


# Define color variables
RED='\033[0;31m'    # Red for parameters
GREEN='\033[0;32m'  # Green for data/values
NC='\033[0m'       # No Color (Reset)


echo "------------------------------------------------"
echo "Deployment Complete!"
echo "Service: $SERVICE_NAME"
echo "Status: $(systemctl is-active $SERVICE_NAME)"
echo "Local IP: $LOCAL_IP"
echo "Public IP: $PUBLIC_IP"
echo "Logs: tail -f $INSTALL_DIR/log/stdout.log"
echo "------------------------------------------------"

# Use echo -e to interpret the escape codes
echo -e "${RED}Private Key:${NC} ${GREEN}$RAW_PRIVATE_KEY${NC}"
echo -e "${RED}Wallet Address:${NC} ${GREEN}$WALLET_ADDRESS${NC}"

# 定义颜色
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}================================================================${NC}"
echo -e "${YELLOW}             SECURITY NOTICE / 安全提示                          ${NC}"
echo -e "${YELLOW}================================================================${NC}"

echo -e "${RED}IMPORTANT:${NC} ${GREEN}The raw 'Private Key' listed above is your ONLY way to${NC}"
echo -e "${GREEN}access your funds. Please SAVE IT in a secure offline location.${NC}"

echo -e "${RED}WARNING:${NC} ${GREEN}DO NOT store the raw Private Key on this server.${NC}"
echo -e "${GREEN}Delete any temporary files or command history containing the key.${NC}"

echo -e "${BLUE}NOTE:${NC} ${GREEN}The configuration in 'shardora.conf' uses a SEALED (encrypted)${NC}"
echo -e "${GREEN}version of your key. Even if the config file is leaked, your${NC}"
echo -e "${GREEN}original private key remains safe and cannot be easily reversed.${NC}"

echo -e "${YELLOW}================================================================${NC}"
