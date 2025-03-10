// Copyright 2004-present Facebook. All Rights Reserved.
#include <iostream>
#include <vector>

#include <folly/Benchmark.h>
#include <folly/Random.h>
#include <folly/init/Init.h>

#include <fizz/crypto/Utils.h>
#include <fizz/crypto/aead/AESGCM128.h>
#include <fizz/crypto/aead/AESOCB128.h>
#include <fizz/crypto/aead/OpenSSLEVPCipher.h>
#include <fizz/record/EncryptedRecordLayer.h>

using namespace fizz;

std::unique_ptr<folly::IOBuf> makeRandomOne(size_t n) {
  static const char alphanum[] =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";

  std::string rv;
  rv.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    rv.push_back(alphanum[folly::Random::rand32() % (sizeof(alphanum) - 1)]);
  }
  return folly::IOBuf::copyBuffer(rv, 5, 17);
}

std::unique_ptr<folly::IOBuf> makeRandom(size_t n, size_t num = 1) {
  std::unique_ptr<folly::IOBuf> ret;
  size_t one = n / num;
  if (!one) {
    one = 1;
  }

  while (n) {
    size_t curr = (n > one) ? one : n;
    auto buf = makeRandomOne(curr);
    if (!ret) {
      ret = std::move(buf);
    } else {
      ret->prependChain(std::move(buf));
    }

    n -= curr;
  }
  return ret;
}

std::unique_ptr<folly::IOBuf> toIOBuf(std::string hexData) {
  std::string out;
  CHECK(folly::unhexlify(hexData, out));
  return folly::IOBuf::copyBuffer(out);
}

TrafficKey getKey() {
  TrafficKey trafficKey;
  trafficKey.key = toIOBuf("000102030405060708090A0B0C0D0E0F");
  trafficKey.iv = toIOBuf("000102030405060708090A0B");
  return trafficKey;
}

void encryptGCM(uint32_t n, size_t size, size_t num) {
  std::unique_ptr<Aead> aead;
  std::vector<fizz::TLSMessage> msgs;
  EncryptedWriteRecordLayer write{EncryptionLevel::AppTraffic};
  BENCHMARK_SUSPEND {
    aead = OpenSSLEVPCipher::makeCipher<AESGCM128>();
    aead->setKey(getKey());
    write.setAead(folly::ByteRange(), std::move(aead));
    for (size_t i = 0; i < n; ++i) {
      TLSMessage msg{ContentType::application_data, makeRandom(size, num)};
      msgs.push_back(std::move(msg));
    }
  }

  TLSContent content;
  for (auto& msg : msgs) {
    content = write.write(std::move(msg), Aead::AeadOptions());
  }
  folly::doNotOptimizeAway(content);
}

void decryptGCM(uint32_t n, size_t size) {
  std::vector<folly::IOBufQueue> contents;
  EncryptedReadRecordLayer read{EncryptionLevel::AppTraffic};
  BENCHMARK_SUSPEND {
    EncryptedWriteRecordLayer write{EncryptionLevel::AppTraffic};
    auto writeAead = OpenSSLEVPCipher::makeCipher<AESGCM128>();
    auto readAead = OpenSSLEVPCipher::makeCipher<AESGCM128>();
    writeAead->setKey(getKey());
    readAead->setKey(getKey());
    write.setAead(folly::ByteRange(), std::move(writeAead));
    read.setAead(folly::ByteRange(), std::move(readAead));
    for (size_t i = 0; i < n; ++i) {
      TLSMessage msg{ContentType::application_data, makeRandom(size)};
      auto content = write.write(std::move(msg), Aead::AeadOptions());
      folly::IOBufQueue queue{folly::IOBufQueue::cacheChainLength()};
      queue.append(std::move(content.data));
      folly::doNotOptimizeAway(queue.front());
      contents.push_back(std::move(queue));
    }
  }

  ReadRecordLayer::ReadResult<TLSMessage> msg;
  for (auto& buf : contents) {
    msg = read.read(buf, Aead::AeadOptions());
  }
  folly::doNotOptimizeAway(msg);
}

void decryptGCMNoRecord(uint32_t n, size_t size) {
  std::unique_ptr<Aead> readAead;
  folly::IOBufQueue queue{folly::IOBufQueue::cacheChainLength()};
  std::vector<std::unique_ptr<folly::IOBuf>> contents;
  auto aad = folly::IOBuf::copyBuffer("aad");
  BENCHMARK_SUSPEND {
    std::unique_ptr<Aead> writeAead = OpenSSLEVPCipher::makeCipher<AESGCM128>();
    readAead = OpenSSLEVPCipher::makeCipher<AESGCM128>();
    writeAead->setKey(getKey());
    readAead->setKey(getKey());
    for (size_t i = 0; i < n; ++i) {
      auto out = writeAead->encrypt(makeRandom(size), aad.get(), 0);
      contents.push_back(std::move(out));
    }
  }

  std::unique_ptr<folly::IOBuf> in;
  for (auto& buf : contents) {
    in = readAead->decrypt(std::move(buf), aad.get(), 0);
  }
  folly::doNotOptimizeAway(in);
}

void touchEveryByte(uint32_t n, size_t size) {
  std::vector<std::unique_ptr<folly::IOBuf>> contents;
  BENCHMARK_SUSPEND {
    for (size_t i = 0; i < n; ++i) {
      contents.push_back(makeRandom(size));
    }
  }

  int isTrue = 0;
  for (auto& buf : contents) {
    for (size_t i = 0; i < buf->length(); ++i) {
      isTrue ^= buf->data()[i];
    }
  }
  folly::doNotOptimizeAway(isTrue);
}

BENCHMARK_NAMED_PARAM(encryptGCM, 10_1, 10, 1);
BENCHMARK_NAMED_PARAM(encryptGCM, 100_1, 100, 1);
BENCHMARK_NAMED_PARAM(encryptGCM, 1000_1, 1000, 1);
BENCHMARK_NAMED_PARAM(encryptGCM, 4000_1, 4000, 1);
BENCHMARK_NAMED_PARAM(encryptGCM, 8000_1, 8000, 1);

BENCHMARK_NAMED_PARAM(encryptGCM, 10_2, 10, 2);
BENCHMARK_NAMED_PARAM(encryptGCM, 100_2, 100, 2);
BENCHMARK_NAMED_PARAM(encryptGCM, 1000_2, 1000, 2);
BENCHMARK_NAMED_PARAM(encryptGCM, 4000_2, 4000, 2);
BENCHMARK_NAMED_PARAM(encryptGCM, 8000_2, 8000, 2);

BENCHMARK_NAMED_PARAM(encryptGCM, 10_4, 10, 4);
BENCHMARK_NAMED_PARAM(encryptGCM, 100_4, 100, 4);
BENCHMARK_NAMED_PARAM(encryptGCM, 1000_4, 1000, 4);
BENCHMARK_NAMED_PARAM(encryptGCM, 4000_4, 4000, 4);
BENCHMARK_NAMED_PARAM(encryptGCM, 8000_4, 8000, 4);

BENCHMARK_PARAM(decryptGCM, 10);
BENCHMARK_PARAM(decryptGCM, 1000);
BENCHMARK_PARAM(decryptGCM, 8000);

BENCHMARK_PARAM(decryptGCMNoRecord, 10);
BENCHMARK_PARAM(decryptGCMNoRecord, 1000);
BENCHMARK_PARAM(decryptGCMNoRecord, 8000);

BENCHMARK_PARAM(touchEveryByte, 10);
BENCHMARK_PARAM(touchEveryByte, 1000);
BENCHMARK_PARAM(touchEveryByte, 8000);

#if FOLLY_OPENSSL_IS_110 && !defined(OPENSSL_NO_OCB)
void encryptOCB(uint32_t n, size_t size) {
  std::unique_ptr<Aead> aead;
  std::vector<fizz::TLSMessage> msgs;
  EncryptedWriteRecordLayer write{EncryptionLevel::AppTraffic};
  BENCHMARK_SUSPEND {
    aead = OpenSSLEVPCipher::makeCipher<AESOCB128>();
    aead->setKey(getKey());
    write.setAead(folly::ByteRange(), std::move(aead));
    for (size_t i = 0; i < n; ++i) {
      TLSMessage msg{ContentType::application_data, makeRandom(size)};
      msgs.push_back(std::move(msg));
    }
  }

  TLSContent content;
  for (auto& msg : msgs) {
    content = write.write(std::move(msg), Aead::AeadOptions());
  }
  folly::doNotOptimizeAway(content);
}

BENCHMARK_PARAM(encryptOCB, 10);
BENCHMARK_PARAM(encryptOCB, 100);
BENCHMARK_PARAM(encryptOCB, 1000);
BENCHMARK_PARAM(encryptOCB, 4000);
BENCHMARK_PARAM(encryptOCB, 8000);
#endif

int main(int argc, char** argv) {
  folly::init(&argc, &argv);
  CryptoUtils::init();
  folly::runBenchmarks();
  return 0;
}
