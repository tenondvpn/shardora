#!/bin/bash
# Install dependencies for shardora3.py (offline-friendly)
# Run: bash install_deps.sh

PYTHON=/root/tools/python3.10/bin/python3
PIP=/root/tools/python3.10/bin/pip3

echo "====== Installing liboqs-python ======"
# liboqs-python requires liboqs shared library
if ! $PYTHON -c "import oqs" 2>/dev/null; then
    # Try pip install
    $PIP install liboqs-python 2>/dev/null || \
    $PIP install oqs 2>/dev/null || \
    echo "⚠️  liboqs-python not available via pip (may need manual install)"
else
    echo "✅ liboqs already installed"
fi

echo ""
echo "====== Installing gmssl ======"
if ! $PYTHON -c "import gmssl" 2>/dev/null; then
    $PIP install gmssl
else
    echo "✅ gmssl already installed"
fi

echo ""
echo "====== Registering solc 0.8.34 with solcx ======"
SOLC_BIN=/usr/local/bin/solc
SOLCX_DIR=$HOME/.solcx
SOLCX_TARGET=$SOLCX_DIR/solc-v0.8.34

if [ -f "$SOLC_BIN" ]; then
    mkdir -p "$SOLCX_DIR"
    if [ ! -f "$SOLCX_TARGET" ]; then
        cp "$SOLC_BIN" "$SOLCX_TARGET"
        chmod +x "$SOLCX_TARGET"
        echo "✅ Copied solc to $SOLCX_TARGET"
    else
        echo "✅ solc-v0.8.34 already registered"
    fi
    $PYTHON -c "import solcx; solcx.set_solc_version('0.8.34'); print('✅ solcx using', solcx.get_solc_version())"
else
    echo "⚠️  /usr/local/bin/solc not found"
fi

echo ""
echo "====== Checking all dependencies ======"
$PYTHON -c "
deps = ['oqs', 'gmssl', 'solcx', 'eth_account', 'ecdsa', 'requests', 'websocket']
for d in deps:
    try:
        __import__(d)
        print(f'  ✅ {d}')
    except ImportError:
        print(f'  ❌ {d} - NOT installed')
"

echo ""
echo "Done."
