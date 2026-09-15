<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Offline TLS fixture

These keys and certificates exist only for Trait OS's deterministic local TLS
peer. They are public test material and must never be installed as production
trust roots. The fixed realtime used by the host client is 2026-08-31 12:00 UTC.

`ca.pem` is the trusted offline root. `anchor.txt` is the same root's DER
subject name and RSA public key encoded for the BearSSL test client. The valid,
expired, future, and untrusted leaf records all use the hostname
`repo.trait.test`; the untrusted leaf is signed by a deliberately absent root.
The Python peer consumes the committed PEM records without network access or
certificate generation.

SHA-256:

```text
3882351b7bb204e254b1d6da3399fad5a96fee2e4c469be34a2ea67edb9c6bf9  anchor.txt
dd5fa3e0e637f6cfb33bab3356ca213e1fbb7fa8936fc6a1874b6e22ddcf17af  ca.pem
c53d5b423ec33fdd56b57276a0156cc688d564deccd4b0f91fccc66db6a031d1  expired-key.pem
e25743d485440c1a9ccf68b1dea17b6cd2aef715c7b094be141efc60e3348fb9  expired.pem
fdb277947166b5609c2c1637d03eae05dec2a6d9a8f26bedff68e4af5a3c1bca  future-key.pem
0d59c881f220452a367025d67330d999318a6a0aa2fa122dc1364f04c8a2b5c2  future.pem
b0888b43ac197782c2272bc928c258c14bac38117fabd2cfd2d9a21b350683dd  untrusted-key.pem
1a4d7c6c66cab574a6333fad71d41584fa8f54efa8ba2f6f48073569ea24301c  untrusted.pem
4a650cf6c640df1ba6a20376b31c8daa7a1a15f01623de3a41b09a25bd66a237  valid-key.pem
d45198691e70d3190ed7cc80193a7aa657e85c1ceb645993f91a35fe2f13acdd  valid.pem
```
