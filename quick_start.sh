#!/bin/bash

# Shardora HTTPS 服务器快速启动脚本
# 自动完成依赖安装、编译和启动

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

echo_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

echo_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检测操作系统
detect_os() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        OS="macos"
    elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
        if [ -f /etc/os-release ]; then
            . /etc/os-release
            OS=$ID
        else
            OS="linux"
        fi
    else
        echo_error "不支持的操作系统: $OSTYPE"
        exit 1
    fi
    echo_info "检测到操作系统: $OS"
}

# 检查依赖
check_dependencies() {
    echo_info "检查依赖..."
    
    local missing_deps=()
    
    # 检查 CMake
    if ! command -v cmake &> /dev/null; then
        missing_deps+=("cmake")
    fi
    
    # 检查 OpenSSL
    if ! command -v openssl &> /dev/null; then
        missing_deps+=("openssl")
    fi
    
    # 检查编译器
    if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
        missing_deps+=("g++ or clang++")
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        echo_error "缺少以下依赖: ${missing_deps[*]}"
        echo_info "请先安装依赖，参考 BUILD_GUIDE.md"
        exit 1
    fi
    
    echo_info "✓ 所有依赖已满足"
}

# 安装 uWebSockets
install_uwebsockets() {
    echo_info "安装 uWebSockets..."
    
    if [ -f "third_party/include/uWebSockets/App.h" ] && [ -f "third_party/lib/libuSockets.a" ]; then
        echo_info "✓ uWebSockets 已安装"
        return 0
    fi
    
    if [ -f "install_uwebsockets.sh" ]; then
        bash install_uwebsockets.sh
    else
        echo_error "找不到 install_uwebsockets.sh"
        exit 1
    fi
}

# 生成 SSL 证书
generate_certificates() {
    echo_info "检查 SSL 证书..."
    
    if [ -f "server-cert.pem" ] && [ -f "server-key.pem" ]; then
        echo_info "✓ SSL 证书已存在"
        return 0
    fi
    
    echo_info "生成自签名 SSL 证书..."
    openssl req -x509 -newkey rsa:4096 \
        -keyout server-key.pem \
        -out server-cert.pem \
        -days 365 -nodes \
        -subj "/C=CN/ST=State/L=City/O=Organization/CN=localhost" \
        2>/dev/null
    
    if [ $? -eq 0 ]; then
        echo_info "✓ SSL 证书生成成功"
        chmod 600 server-key.pem
        chmod 644 server-cert.pem
    else
        echo_error "SSL 证书生成失败"
        exit 1
    fi
}

# 编译项目
build_project() {
    echo_info "编译项目..."
    
    # 创建构建目录
    mkdir -p build
    cd build
    
    # 配置 CMake
    echo_info "配置 CMake..."
    if [[ "$OS" == "macos" ]]; then
        # macOS 需要指定 OpenSSL 路径
        OPENSSL_ROOT=$(brew --prefix openssl@3 2>/dev/null || echo "/usr/local/opt/openssl")
        cmake -DCMAKE_BUILD_TYPE=Release \
              -DOPENSSL_ROOT_DIR="$OPENSSL_ROOT" \
              .. || {
            echo_error "CMake 配置失败"
            exit 1
        }
    else
        cmake -DCMAKE_BUILD_TYPE=Release .. || {
            echo_error "CMake 配置失败"
            exit 1
        }
    fi
    
    # 编译
    echo_info "开始编译（这可能需要几分钟）..."
    local cpu_cores=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    make shardora -j$cpu_cores || {
        echo_error "编译失败"
        exit 1
    }
    
    cd ..
    echo_info "✓ 编译成功"
}

# 运行测试
run_tests() {
    echo_info "运行基本测试..."
    
    if [ ! -f "build/shardora" ]; then
        echo_error "找不到可执行文件 build/shardora"
        exit 1
    fi
    
    # 检查依赖库
    if [[ "$OS" == "macos" ]]; then
        otool -L build/shardora | grep -E "(ssl|crypto)" > /dev/null || {
            echo_warn "警告: 未检测到 OpenSSL 库链接"
        }
    else
        ldd build/shardora | grep -E "(ssl|crypto)" > /dev/null || {
            echo_warn "警告: 未检测到 OpenSSL 库链接"
        }
    fi
    
    echo_info "✓ 基本检查通过"
}

# 启动服务器
start_server() {
    echo_info "准备启动服务器..."
    
    # 复制证书到构建目录
    cp server-cert.pem server-key.pem build/ 2>/dev/null || true
    
    echo ""
    echo "=========================================="
    echo "Shardora HTTPS 服务器已准备就绪"
    echo "=========================================="
    echo ""
    echo "启动服务器:"
    echo "  cd build && ./shardora"
    echo ""
    echo "测试连接:"
    echo "  curl -k https://localhost:8080/query_init"
    echo ""
    echo "Python 测试:"
    echo "  python3 test_https_client.py"
    echo ""
    echo "查看日志:"
    echo "  tail -f build/shardora.log"
    echo ""
    echo "=========================================="
}

# 主函数
main() {
    echo ""
    echo "=========================================="
    echo "Shardora HTTPS 服务器快速启动"
    echo "=========================================="
    echo ""
    
    # 检测操作系统
    detect_os
    
    # 检查依赖
    check_dependencies
    
    # 安装 uWebSockets
    install_uwebsockets
    
    # 生成证书
    generate_certificates
    
    # 编译项目
    build_project
    
    # 运行测试
    run_tests
    
    # 启动提示
    start_server
    
    echo_info "✓ 所有步骤完成！"
}

# 运行主函数
main "$@"
