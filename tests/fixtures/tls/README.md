<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Offline TLS fixture

These keys and certificates exist only for OpenRFS's deterministic local TLS
peer. They are public test material and must never be installed as production
trust roots. The fixed realtime used by the host client is 2026-08-31 12:00 UTC.

`ca.pem` is the trusted offline root. `anchor.txt` is the same root's DER
subject name and RSA public key encoded for the BearSSL test client. The valid,
expired, future, and untrusted leaf records all use the hostname
`repo.openrfs.test`; the untrusted leaf is signed by a deliberately absent root.
The Python peer consumes the committed PEM records without network access or
certificate generation.

SHA-256:

```text
525283523a4c8f7818bc295e52011e138118105566321d513e0f558fa60c4390  anchor.txt
c2bd01e6ea97d351f951be5758b6ea92841a9d41e02d9e3e4f10c768ffffc167  ca.pem
c53d5b423ec33fdd56b57276a0156cc688d564deccd4b0f91fccc66db6a031d1  expired-key.pem
ccb71eca584d5145e751b7f9f60b01abda0294f8739d71ab6d39ef6662f839f0  expired.pem
fdb277947166b5609c2c1637d03eae05dec2a6d9a8f26bedff68e4af5a3c1bca  future-key.pem
7e5cb0759a684611fd2e8f392375f354fb9cccc028d9aea3efd0ecea58a99658  future.pem
b0888b43ac197782c2272bc928c258c14bac38117fabd2cfd2d9a21b350683dd  untrusted-key.pem
cb4a47d687b42fba6f214d7112ea753419bae00fee0785dc76fa7970e6f5c036  untrusted.pem
4a650cf6c640df1ba6a20376b31c8daa7a1a15f01623de3a41b09a25bd66a237  valid-key.pem
370f4e43929cb687200249b1de0c8b914f8766bd46b0c62e9e90a910f69eeed6  valid.pem
```
