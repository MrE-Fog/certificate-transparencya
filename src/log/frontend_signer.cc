#include <assert.h>
#include <stdint.h>

#include "ct.pb.h"
#include "frontend_signer.h"
#include "log_db.h"
#include "log_signer.h"
#include "serial_hasher.h"
#include "submission_handler.h"
#include "util.h"

FrontendSigner::FrontendSigner(LogDB *db, LogSigner *signer)
    : db_(db),
      hasher_(new Sha256Hasher),
      signer_(signer),
      // Default handler.
      handler_(new SubmissionHandler()) {
  assert(signer_ != NULL);
  assert(db_ != NULL);
}

FrontendSigner::FrontendSigner(LogDB *db, LogSigner *signer,
                               SubmissionHandler *handler)
    : db_(db),
      hasher_(new Sha256Hasher),
      signer_(signer),
      handler_(handler) {
  assert(signer_ != NULL);
  assert(db_ != NULL);
  assert(handler_ != NULL);
}

FrontendSigner::~FrontendSigner() {
  delete db_;
  delete hasher_;
  delete signer_;
  delete handler_;
}

FrontendSigner::SubmitResult
FrontendSigner::QueueEntry(const bstring &data,
                           SignedCertificateTimestamp *sct) {
  return QueueEntry(CertificateEntry::X509_ENTRY, data, sct);
}

FrontendSigner::SubmitResult
FrontendSigner::QueueEntry(CertificateEntry::Type type,
                           const bstring data,
                           SignedCertificateTimestamp *sct) {
  // Verify the submission and compute signed and unsigned parts.
  CertificateEntry entry;
  entry.set_type(type);
  SubmissionHandler::SubmitResult result =
      handler_->ProcessSubmission(data, &entry);
  if (result != SubmissionHandler::OK)
    return GetSubmitError(result);

  // Check if the entry already exists.
  bstring primary_key = ComputePrimaryKey(entry);
  assert(!primary_key.empty());

  bstring record;
  LogDB::Status status = db_->LookupEntry(primary_key, LogDB::ANY, &record);
  if (status == LogDB::LOGGED || status == LogDB::PENDING) {
    if (sct != NULL)
      sct->ParseFromString(record);
    if (status == LogDB::LOGGED)
      return LOGGED;
    return PENDING;
  }

  assert(status == LogDB::NOT_FOUND);

  SignedCertificateTimestamp local_sct;
  local_sct.mutable_entry()->CopyFrom(entry);

  TimestampAndSign(&local_sct);

  local_sct.SerializeToString(&record);
  status = db_->WriteEntry(primary_key, record);

  // Assume for now that nobody interfered while we were busy signing.
  assert(status == LogDB::NEW);
  if (sct != NULL)
    sct->CopyFrom(local_sct);
  return NEW;
}

// static
std::string FrontendSigner::SubmitResultString(SubmitResult result) {
  std::string result_string;
  switch (result) {
    case LOGGED:
      result_string = "submission already logged";
      break;
    case PENDING:
      result_string = "submission already pending";
      break;
    case NEW:
      result_string = "new submission accepted";
      break;
    case BAD_PEM_FORMAT:
      result_string = "not a valid PEM-encoded chain";
      break;
      // TODO(ekasper): the following two could/should be more precise.
    case SUBMISSION_TOO_LONG:
      result_string = "DER-encoded certificate chain length "
          "exceeds allowed limit";
      break;
    case CERTIFICATE_VERIFY_ERROR:
      result_string = "could not verify certificate chain";
      break;
    case PRECERT_CHAIN_NOT_WELL_FORMED:
      result_string = "precert chain not well-formed";
      break;
    case UNKNOWN_ERROR:
      result_string = "unknown error";
      break;
    default:
      assert(false);
  }

  return result_string;
}

bstring FrontendSigner::ComputePrimaryKey(const CertificateEntry &entry) const {
  // Compute the SHA-256 hash of the leaf certificate.
  hasher_->Reset();
  hasher_->Update(entry.leaf_certificate());
  return hasher_->Final();
}

void FrontendSigner::TimestampAndSign(SignedCertificateTimestamp *sct) const {
  sct->set_timestamp(util::TimeInMilliseconds());
  // The submission handler has already verified the format of this entry,
  // so this should never fail.
  LogSigner::SignResult ret = signer_->SignCertificateTimestamp(sct);
  assert(ret == LogSigner::OK);
}

// static
FrontendSigner::SubmitResult
FrontendSigner::GetSubmitError(SubmissionHandler::SubmitResult result) {
  SubmitResult submit_result = UNKNOWN_ERROR;
  switch (result) {
    case SubmissionHandler::EMPTY_SUBMISSION:
    case SubmissionHandler::INVALID_PEM_ENCODED_CHAIN:
      submit_result = BAD_PEM_FORMAT;
      break;
    case SubmissionHandler::SUBMISSION_TOO_LONG:
      submit_result = SUBMISSION_TOO_LONG;
      break;
    case SubmissionHandler::INVALID_CERTIFICATE_CHAIN:
    case SubmissionHandler::UNKNOWN_ROOT:
      submit_result = CERTIFICATE_VERIFY_ERROR;
      break;
    case SubmissionHandler::PRECERT_CHAIN_NOT_WELL_FORMED:
      submit_result = PRECERT_CHAIN_NOT_WELL_FORMED;
      break;
    default:
      assert(false);
  }
  return submit_result;
}