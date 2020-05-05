/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree.
 */

#include <fizz/crypto/openssl/OpenSSL.h>
#include <fizz/crypto/openssl/OpenSSLKeyUtils.h>
#include <folly/io/IOBuf.h>
#include <folly/lang/Assume.h>

namespace fizz {

namespace detail {

std::unique_ptr<folly::IOBuf> ecSign(
    folly::ByteRange data,
    const folly::ssl::EvpPkeyUniquePtr& pkey,
    int hashNid);

void ecVerify(
    folly::ByteRange data,
    folly::ByteRange signature,
    const folly::ssl::EvpPkeyUniquePtr& pkey,
    int hashNid);

#if FIZZ_OPENSSL_HAS_ED25519
std::unique_ptr<folly::IOBuf> edSign(
    folly::ByteRange data,
    const folly::ssl::EvpPkeyUniquePtr& pkey);

void edVerify(
    folly::ByteRange data,
    folly::ByteRange signature,
    const folly::ssl::EvpPkeyUniquePtr& pkey);
#endif

std::unique_ptr<folly::IOBuf> rsaPssSign(
    folly::ByteRange data,
    const folly::ssl::EvpPkeyUniquePtr& pkey,
    int hashNid);

void rsaPssVerify(
    folly::ByteRange data,
    folly::ByteRange signature,
    const folly::ssl::EvpPkeyUniquePtr& pkey,
    int hashNid);
} // namespace detail

template <SignatureScheme Scheme>
struct SigAlg {};

template <>
struct SigAlg<SignatureScheme::rsa_pss_sha256> {
  static constexpr int HashNid = NID_sha256;
  static constexpr KeyType type = KeyType::RSA;
};

template <>
struct SigAlg<SignatureScheme::ecdsa_secp256r1_sha256> {
  static constexpr int HashNid = NID_sha256;
  static constexpr KeyType type = KeyType::P256;
};

template <>
struct SigAlg<SignatureScheme::ecdsa_secp384r1_sha384> {
  static constexpr int HashNid = NID_sha384;
  static constexpr KeyType type = KeyType::P384;
};

template <>
struct SigAlg<SignatureScheme::ecdsa_secp521r1_sha512> {
  static constexpr int HashNid = NID_sha512;
  static constexpr KeyType type = KeyType::P521;
};

template <KeyType Type>
template <SignatureScheme Scheme>
inline std::unique_ptr<folly::IOBuf> OpenSSLSignature<Type>::sign(
    folly::ByteRange data) const {
  static_assert(
      SigAlg<Scheme>::type == Type, "Called with mismatched type and scheme");
  switch (Type) {
    case KeyType::P256:
    case KeyType::P384:
    case KeyType::P521:
      return detail::ecSign(data, pkey_, SigAlg<Scheme>::HashNid);
    case KeyType::RSA:
      return detail::rsaPssSign(data, pkey_, SigAlg<Scheme>::HashNid);
  }
  folly::assume_unreachable();
}

template <KeyType Type>
template <SignatureScheme Scheme>
inline void OpenSSLSignature<Type>::verify(
    folly::ByteRange data,
    folly::ByteRange signature) const {
  switch (Type) {
    case KeyType::P256:
    case KeyType::P384:
    case KeyType::P521:
      return detail::ecVerify(data, signature, pkey_, SigAlg<Scheme>::HashNid);
    case KeyType::RSA:
      return detail::rsaPssVerify(
          data, signature, pkey_, SigAlg<Scheme>::HashNid);
  }
  folly::assume_unreachable();
}

template <>
inline void OpenSSLSignature<KeyType::P256>::setKey(
    folly::ssl::EvpPkeyUniquePtr pkey) {
  detail::validateECKey(pkey, NID_X9_62_prime256v1);
  pkey_ = std::move(pkey);
}

template <>
inline void OpenSSLSignature<KeyType::P384>::setKey(
    folly::ssl::EvpPkeyUniquePtr pkey) {
  detail::validateECKey(pkey, NID_secp384r1);
  pkey_ = std::move(pkey);
}

template <>
inline void OpenSSLSignature<KeyType::P521>::setKey(
    folly::ssl::EvpPkeyUniquePtr pkey) {
  detail::validateECKey(pkey, NID_secp521r1);
  pkey_ = std::move(pkey);
}

template <>
inline void OpenSSLSignature<KeyType::RSA>::setKey(
    folly::ssl::EvpPkeyUniquePtr pkey) {
  if (EVP_PKEY_id(pkey.get()) != EVP_PKEY_RSA) {
    throw std::runtime_error("key not rsa");
  }
  pkey_ = std::move(pkey);
}
} // namespace fizz
