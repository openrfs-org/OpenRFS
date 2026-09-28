<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Offline TLS fixture

These keys and certificates exist only for RSD's deterministic local TLS
peer. They are public test material and must never be installed as production
trust roots. The fixed realtime used by the host client is 2026-08-31 12:00 UTC.

`ca.pem` is the trusted offline root. `anchor.txt` is the same root's DER
subject name and RSA public key encoded for the BearSSL test client. The valid,
expired, future, and untrusted leaf records all use the hostname
`repo.rsd.test`; the untrusted leaf is signed by a deliberately absent root.
The Python peer consumes the committed PEM records without network access or
certificate generation.

SHA-256:

```text
6b709ad129d36d11b3adae8e985ec8e54ada9dde7250dc63f454d1f765902138  anchor.txt
66846715fc14c44dab45ad21bead3d4f6b7728c485d128dcfad57f1547428896  ca.pem
c53d5b423ec33fdd56b57276a0156cc688d564deccd4b0f91fccc66db6a031d1  expired-key.pem
e47b1d37b293b80026ef8396e0088627093d076e5449054a76842d0483cdc2cd  expired.pem
fdb277947166b5609c2c1637d03eae05dec2a6d9a8f26bedff68e4af5a3c1bca  future-key.pem
684011ca4470987095a585aa724a4b7e05a61f1e346a8eba391368a013c4fd51  future.pem
b0888b43ac197782c2272bc928c258c14bac38117fabd2cfd2d9a21b350683dd  untrusted-key.pem
0bc9677612b525580c1333403a32dad6ca5b4b3a04da32e250c8ed71ab0e810d  untrusted.pem
4a650cf6c640df1ba6a20376b31c8daa7a1a15f01623de3a41b09a25bd66a237  valid-key.pem
95cb246cb482682bf18c0ed8ebd0711510def44a96871e8741fa0b9b7960359c  valid.pem
```
