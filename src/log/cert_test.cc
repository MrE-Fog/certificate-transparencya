#include <gtest/gtest.h>
#include <openssl/ssl.h>
#include <string>

#include "../util/util.h"
#include "cert.h"

static const char kCertDir[] = "../test/testdata";

// TODO: add test certs with intermediates.
// Valid certificates.
static const char kCaCert[] = "ca-cert.pem";
// Issued by ca-cert.pem
static const char kLeafCert[] = "test-cert.pem";
// Issued by ca-cert.pem
static const char kCaProtoCert[] = "ca-proto-cert.pem";
// Issued by ca-protocert.pem
static const char kProtoCert[] = "test-proto-cert.pem";

namespace {

class CertTest : public ::testing::Test {
 protected:
  std::string leaf_pem_;
  std::string ca_pem_;
  std::string ca_protocert_pem_;
  std::string protocert_pem_;

  void SetUp() {
    const std::string cert_dir = std::string(kCertDir);
    ASSERT_TRUE(util::ReadTextFile(cert_dir + "/" + kLeafCert, &leaf_pem_));
    ASSERT_TRUE(util::ReadTextFile(cert_dir + "/" + kCaCert, &ca_pem_));
    ASSERT_TRUE(util::ReadTextFile(cert_dir + "/" + kCaProtoCert,
                                   &ca_protocert_pem_));
    ASSERT_TRUE(util::ReadTextFile(cert_dir + "/" + kProtoCert,
                                   &protocert_pem_));
  }
};

TEST_F(CertTest, Cert) {
  Cert leaf(leaf_pem_);
  ASSERT_TRUE(leaf.IsLoaded());

  Cert ca(ca_pem_);
  ASSERT_TRUE(ca.IsLoaded());

  Cert ca_proto(ca_protocert_pem_);
  ASSERT_TRUE(ca_proto.IsLoaded());

  Cert proto(protocert_pem_);
  ASSERT_TRUE(proto.IsLoaded());

  // Some facts we know are true about those test certs.
  EXPECT_TRUE(leaf.HasExtension(NID_authority_key_identifier));
  EXPECT_TRUE(ca.HasExtension(NID_authority_key_identifier));

  EXPECT_TRUE(leaf.HasExtension(NID_basic_constraints));
  EXPECT_TRUE(ca.HasExtension(NID_basic_constraints));

  EXPECT_FALSE(leaf.HasBasicConstraintCA());
  EXPECT_TRUE(ca.HasBasicConstraintCA());
  EXPECT_TRUE(leaf.IsIssuedBy(ca));
  EXPECT_TRUE(leaf.IsSignedBy(ca));

  EXPECT_FALSE(ca.IsIssuedBy(leaf));
  EXPECT_FALSE(ca.IsSignedBy(leaf));

  // Some more extensions.
  EXPECT_TRUE(ca_proto.HasExtendedKeyUsage(Cert::kCtExtendedKeyUsageOID));
  EXPECT_TRUE(proto.HasExtension(Cert::kPoisonExtensionOID));
  EXPECT_TRUE(proto.IsCriticalExtension(Cert::kPoisonExtensionOID));

  // Bogus certs.
  Cert invalid("");
  EXPECT_FALSE(invalid.IsLoaded());

  Cert invalid2("-----BEGIN CERTIFICATE-----invalid-----END CERTIFICATE-----");
  EXPECT_FALSE(invalid2.IsLoaded());
}

TEST_F(CertTest, CertChain) {
  // A single certificate.
  CertChain chain(leaf_pem_);
  ASSERT_TRUE(chain.IsLoaded());

  EXPECT_EQ(chain.Length(), 1U);
  EXPECT_TRUE(chain.IsValidIssuerChain());
  EXPECT_TRUE(chain.IsValidSignatureChain());

  // Add its issuer.
  chain.AddCert(new Cert(ca_pem_));
  ASSERT_TRUE(chain.IsLoaded());
  EXPECT_EQ(chain.Length(), 2U);
  EXPECT_TRUE(chain.IsValidIssuerChain());
  EXPECT_TRUE(chain.IsValidSignatureChain());

  // In reverse order.
  CertChain chain2(ca_pem_);
  ASSERT_TRUE(chain2.IsLoaded());
  EXPECT_EQ(chain2.Length(), 1U);
  EXPECT_TRUE(chain2.IsValidIssuerChain());
  EXPECT_TRUE(chain2.IsValidSignatureChain());

  chain2.AddCert(new Cert(leaf_pem_));
  ASSERT_TRUE(chain2.IsLoaded());
  EXPECT_EQ(chain2.Length(), 2U);
  EXPECT_FALSE(chain2.IsValidIssuerChain());
  EXPECT_FALSE(chain2.IsValidSignatureChain());

  // Invalid
  CertChain invalid("");
  EXPECT_FALSE(invalid.IsLoaded());

  // A chain with three certificates. Construct from concatenated PEM entries.
  std::string pem_bundle = protocert_pem_ + ca_protocert_pem_ + ca_pem_;
  CertChain chain3(pem_bundle);
  ASSERT_TRUE(chain3.IsLoaded());
  EXPECT_EQ(chain3.Length(), 3U);
  EXPECT_TRUE(chain3.IsValidIssuerChain());
  EXPECT_TRUE(chain3.IsValidSignatureChain());
}

TEST_F(CertTest, ProtoCertChain) {
  // A protocert chain.
  std::string pem_bundle = protocert_pem_ + ca_protocert_pem_;
  ProtoCertChain proto_chain(pem_bundle);
  ASSERT_TRUE(proto_chain.IsLoaded());
  EXPECT_EQ(proto_chain.Length(), 2U);
  EXPECT_EQ(proto_chain.IntermediateLength(), 0U);
  EXPECT_TRUE(proto_chain.IsValidIssuerChain());
  EXPECT_TRUE(proto_chain.IsValidSignatureChain());
  EXPECT_TRUE(proto_chain.IsWellFormed());

  // Try to construct a protocert chain from regular certs.
  // The chain should load, but is not well-formed.
  pem_bundle = leaf_pem_ + ca_pem_;
  ProtoCertChain proto_chain2(pem_bundle);
  ASSERT_TRUE(proto_chain2.IsLoaded());
  EXPECT_EQ(proto_chain2.Length(), 2U);
  EXPECT_EQ(proto_chain2.IntermediateLength(), 0U);
  EXPECT_TRUE(proto_chain2.IsValidIssuerChain());
  EXPECT_TRUE(proto_chain2.IsValidSignatureChain());
  EXPECT_FALSE(proto_chain2.IsWellFormed());
}

}  // namespace

int main(int argc, char**argv) {
  ::testing::InitGoogleTest(&argc, argv);
  SSL_library_init();
  return RUN_ALL_TESTS();
}