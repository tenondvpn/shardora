#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
C2CSellOrder Test Suite Setup Script
===================================
Installs required dependencies for the C2C contract test suite.
"""

import os
import sys
import subprocess
import importlib.util

# Set UTF-8 encoding for Windows compatibility
if sys.platform.startswith('win'):
    try:
        import codecs
        sys.stdout = codecs.getwriter('utf-8')(sys.stdout.detach())
        sys.stderr = codecs.getwriter('utf-8')(sys.stderr.detach())
    except:
        pass  # Fallback if encoding setup fails

def check_python_version():
    """Check if Python version is compatible"""
    if sys.version_info < (3, 7):
        print("Error: Python 3.7 or higher is required")
        return False
    print(f"Python version: {sys.version}")
    return True

def install_package(package_name, import_name=None):
    """Install a Python package using pip"""
    if import_name is None:
        import_name = package_name
    
    # Check if package is already installed
    spec = importlib.util.find_spec(import_name)
    if spec is not None:
        print(f"[OK] {package_name} is already installed")
        return True
    
    print(f"Installing {package_name}...")
    try:
        subprocess.check_call([sys.executable, "-m", "pip", "install", package_name])
        print(f"[OK] {package_name} installed successfully")
        return True
    except subprocess.CalledProcessError as e:
        print(f"[FAIL] Failed to install {package_name}: {e}")
        return False

def install_solc():
    """Install Solidity compiler"""
    print("Setting up Solidity compiler...")
    
    # First install py-solc-x
    if not install_package("py-solc-x", "solcx"):
        return False
    
    # Try to install solc
    try:
        import solcx
        print("Installing Solidity compiler version 0.8.19...")
        solcx.install_solc('0.8.19')
        solcx.set_solc_version('0.8.19')
        print("[OK] Solidity compiler installed successfully")
        return True
    except Exception as e:
        print(f"[FAIL] Failed to install Solidity compiler: {e}")
        print("Note: You can still run tests with mock deployment")
        return False

def main():
    """Main setup function"""
    print("C2CSellOrder Test Suite Setup")
    print("=" * 50)
    
    # Check Python version
    if not check_python_version():
        return 1
    
    # Required packages
    packages = [
        ("requests", None),
        ("eth-abi", "eth_abi"),
        ("ecdsa", None),
        ("pycryptodome", "Crypto"),
    ]
    
    success_count = 0
    total_packages = len(packages) + 1  # +1 for solc
    
    # Install basic packages
    for package_name, import_name in packages:
        if install_package(package_name, import_name):
            success_count += 1
    
    # Install Solidity compiler
    if install_solc():
        success_count += 1
    
    print("\n" + "=" * 50)
    print("Setup Summary")
    print("=" * 50)
    print(f"Successfully installed: {success_count}/{total_packages} components")
    
    if success_count == total_packages:
        print("[OK] All dependencies installed successfully!")
        print("\nYou can now run the test suite:")
        print("  python test_c2c_contract.py")
    elif success_count >= total_packages - 1:
        print("[OK] Most dependencies installed successfully!")
        print("[WARN] Solidity compiler installation failed, but you can still run tests with mock deployment")
        print("\nYou can now run the test suite:")
        print("  python test_c2c_contract.py")
    else:
        print("[FAIL] Some dependencies failed to install")
        print("Please install missing dependencies manually:")
        print("  pip install requests eth-abi ecdsa pycryptodome py-solc-x")
    
    print("\nFor more information, see README_C2C_TEST.md")
    return 0 if success_count >= total_packages - 1 else 1

if __name__ == "__main__":
    exit(main())