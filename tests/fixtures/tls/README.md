<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Offline TLS fixture

These keys and certificates exist only for OpenGAT's deterministic local TLS
peer. They are public test material and must never be installed as production
trust roots. The fixed realtime used by the host client is 2026-08-31 12:00 UTC.

`ca.pem` is the trusted offline root. `anchor.txt` is the same root's DER
subject name and RSA public key encoded for the BearSSL test client. The valid,
expired, future, and untrusted leaf records all use the hostname
`repo.opengat.test`; the untrusted leaf is signed by a deliberately absent root.
The Python peer consumes the committed PEM records without network access or
certificate generation.

SHA-256:

```text
ea58c747a67e96b598d30afe7595bdb2c51eed89ceb0c43760a53b72f117cffd  anchor.txt
14cb8aa48ef9bd1017b027c1b780621909134c044c536a88a3bbf20335f7f5e8  ca.pem
c53d5b423ec33fdd56b57276a0156cc688d564deccd4b0f91fccc66db6a031d1  expired-key.pem
0524a8d960b65e534ecc8d64f3a1328356b3ebf0044500400099196c2cccbd49  expired.pem
fdb277947166b5609c2c1637d03eae05dec2a6d9a8f26bedff68e4af5a3c1bca  future-key.pem
cc6461c01a204ad076518d4bb17551a16eda25a92c1252ddfd2d0847e47314d0  future.pem
b0888b43ac197782c2272bc928c258c14bac38117fabd2cfd2d9a21b350683dd  untrusted-key.pem
d0972785f7623fee7689afaeaa9245738147659d55bcefc2e386038fca4bb9db  untrusted.pem
4a650cf6c640df1ba6a20376b31c8daa7a1a15f01623de3a41b09a25bd66a237  valid-key.pem
861be29bc87f69eed17869c3f3b32e12fbacb4fdfc66c25e85aba73a211292be  valid.pem
```
