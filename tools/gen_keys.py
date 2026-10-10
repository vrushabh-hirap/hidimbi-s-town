#!/usr/bin/env python3
"""
tools/gen_keys.py
Generates an RSA 2048-bit keypair for JWT signing between Backend and Flag Service.
Uses Python cryptography library if available, otherwise falls back to openssl CLI.
"""

import os
import subprocess
import sys

def main():
    backend_dir = os.path.join(os.path.dirname(__file__), "..", "backend")
    os.makedirs(backend_dir, exist_ok=True)

    private_key_path = os.path.join(backend_dir, "private_key.pem")
    public_key_path = os.path.join(backend_dir, "public_key.pem")

    if os.path.exists(private_key_path) and os.path.exists(public_key_path):
        print(f"[+] RSA keys already exist at {backend_dir}")
        return

    print("[*] Generating 2048-bit RSA keypair...")
    try:
        from cryptography.hazmat.primitives.asymmetric import rsa
        from cryptography.hazmat.primitives import serialization
        private_key = rsa.generate_private_key(
            public_exponent=65537,
            key_size=2048
        )
        private_pem = private_key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.PKCS8,
            encryption_algorithm=serialization.NoEncryption()
        )
        public_key = private_key.public_key()
        public_pem = public_key.public_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PublicFormat.SubjectPublicKeyInfo
        )
        with open(private_key_path, "wb") as f:
            f.write(private_pem)
        with open(public_key_path, "wb") as f:
            f.write(public_pem)
    except ImportError:
        # Fallback to OpenSSL CLI
        print("[*] Python cryptography module not found, using openssl CLI...")
        subprocess.run(["openssl", "genrsa", "-out", private_key_path, "2048"], check=True)
        subprocess.run(["openssl", "rsa", "-in", private_key_path, "-pubout", "-out", public_key_path], check=True)

    print(f"[+] Saved private key: {private_key_path}")
    print(f"[+] Saved public key:  {public_key_path}")

if __name__ == "__main__":
    main()
