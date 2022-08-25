static const unsigned char rsa_pub_key[] = R"~~~(-----BEGIN PUBLIC KEY-----
MIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEA04D07cMpLUVQCLeNCUB0
IcKhKUG35JExPwqo58w/BviOueU6ibOROxf63kI+yljFg8B2aV1lB5Fi8WeftF6s
dex+Y4t5i/vBC2RlIcO9cNs1yxCVKkpTqMv4j2M9gdjyM5PAsk8VmIG/siPNiI56
MMO+1aSF6aQMaUW1kvIiMQM7d7NoqSuP+DHjYWCKrU2T3eMn/zxa9jIohyQcSfdV
uPJjZuvgmST7qHAk/7YR6lcrbB25+jqrRReloZFEvH0iSMHB+ruAihsVIrLNK6iE
kBF6UN5etYBez210Huouyneb2V7WzbLvBTf3E+fmTMyrZxPL4/DWfz0hhPkWmGpI
j1xLqknr6OTSEQ3f5YWU7byGEvs5fqaMokqR73gNjP5WzTBAFWaiH1PtaezasUtr
WZ7GegTepRvXta+A3XJVnwmhZbxB7uJsRkKxUQsqEMC+RDqH9RFalGZKaP2wrIce
TYTMhbKL6Gg/w7M514yqonIfoul2iKkN3wtlDxU7NL4bAbc6NRidgvOOLVKsNN2p
Oib3h1xgJfpW3y6kODCA71ZK47DkhS/eSR3vXGMJfx2uaas6lg5KiIo0KlHxzzMj
HqoLBoiNUfXqJ6kbAwo2o8/K/pQy06pjCCAKaozJPJ3jQl1Js22SsQKFo45UsQkD
RsvhLheT146a+Cba80NApvsCAwEAAQ==
-----END PUBLIC KEY-----
)~~~";

/*
- This file is for conveniently storing rsa_public_key to use/embed in the fimrware.
- You should only include this file once and only-once in the main.cpp source file.
- ... Then use the rsa_pub_key as a global char pointer to the raw_rsa_pub_key_c_str.
- Usage:
    + Gen the rsa_key.pub as raw format from openssl. 
    + Copy and paste the raw content in rsa_key.pub into the space between a "Raw string literal" as seen above. 
    + `R"~~~(<your_rsa_raw_text_here_without_any_extra_space_or_newline)~~~"` 
*/