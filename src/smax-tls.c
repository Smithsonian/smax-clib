/**
 * @file
 *
 * @date Created  on Jan 13, 2025
 * @author Attila Kovacs
 *
 * @brief TLS configuration for SMA-X.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "smax-private.h"

/// \cond PRIVATE
#if WITH_TLS
typedef struct {
  boolean enabled;        ///< Whether TLS is enabled.
  char *ca_path;          ///< Directory in which CA certificates reside
  char *ca_certificate;   ///< CA sertificate
  boolean skip_verify;    ///< Whether to skip verification of the certificate (insecure)
  char *certificate;      ///< Client certificate (mutual TLS only)
  char *key;              ///< Client private key (mutual TLS only)
  char *dh_params;        ///< (optional) parameter file for DH based ciphers
  char *ciphers;          ///< colon separated list of ciphers to try (TKS v1.2 and earlier)
  char *cipher_suites;    ///< colon separated list of ciphers suites to try (TKS v1.3 and later)
  char *hostname;         ///< Server name for SNI
} TLSConfig;

static TLSConfig config;
#endif

/**
 * (<i>for internal use<i>) Applies the TLS configration for the SMA-X server. The caller
 * should have a lock on the SMA-X configuration mutex, and it should be called after the
 * Redis instance is initialized, but before it is connected.
 *
 * @param redis   The unconnected Redis instance to be used for SMA-X.
 * @return        X_SUCCESS (0) if successful, or else and error code &lt;0.
 *
 * @sa smaxSetTLS()
 */
int smaxConfigTLSAsync(Redis *redis) {
#if WITH_TLS
  static const char *fn = "smaxConfigTLS()";

  if(!redis) return x_error(X_NULL, EINVAL, fn, "redis is NULL");

  if(!config.enabled) return X_SUCCESS;

  prop_error(fn, redisxSetTLS(redis, config.ca_path, config.ca_certificate));
  prop_error(fn, redisxSetTLSVerify(redis, !config.skip_verify));
  prop_error(fn, redisxSetMutualTLS(redis, config.certificate, config.key));
  prop_error(fn, redisxSetTLSServerName(redis, config.hostname));
  prop_error(fn, redisxSetTLSCiphers(redis, config.ciphers));
  prop_error(fn, redisxSetTLSCipherSuites(redis, config.cipher_suites));
  prop_error(fn, redisxSetDHCipherParams(redis, config.dh_params));
#endif
  (void) redis;
  return X_SUCCESS;
}
/// \endcond

/**
 * Configures a TLS-encrypted connection to thr SMA-X server with the specified CA certificate file.
 * Normally you will want to set up mutual TLS with smaxSetMutualTLS() also, unless the server is not
 * requiring mutual authentication. Additionally, you might also want to set parameters for DH-based
 * cyphers if needed using smaxSetDHCypherParams().
 *
 * @param ca_path   Directory containing CA certificates. It may be NULL to use the default locations.
 * @param ca_file   CA certificate file relative to specified directory. It may be NULL to use default
 *                  certificate.
 * @return          X_SUCCESS (0) if successful, or X_NAME_INVALID if the path or CA certificate file
 *                  is not accessible, or else X_FAILURE (-1) if the SMA-X library was built
 *                  without TLS support.
 *
 * @sa smaxDisableTLS()
 * @sa smaxSetMutualTLS()
 * @sa smaxSetDHCipherParams()
 * @sa smaxSetTLSCiphers()
 * @sa smaxSetTLSCipherSuites()
 * @sa smaxSetTLSServerName()
 * @sa smaxSetTLSVerify()
 */
int smaxSetTLS(const char *ca_path, const char *ca_file) {
  static const char *fn = "smaxSetTLS";

#if WITH_TLS
  if(ca_path) if(access(ca_path, R_OK) != 0) return x_error(X_NAME_INVALID, EINVAL, fn, "certificate directory not accessible: %s", ca_path);
  if(ca_file) if(access(ca_file, R_OK) != 0) return x_error(X_NAME_INVALID, EINVAL, fn, "CA certificate not accessible: %s", ca_file);

  smaxLockConfig();
  if(config.ca_path) free(config.ca_path);
  config.ca_path = xStringCopyOf(ca_path);
  if(config.ca_certificate) free(config.ca_certificate);
  config.ca_certificate = xStringCopyOf(ca_file);
  config.enabled = TRUE;
  smaxUnlockConfig();
  return X_SUCCESS;
#else
  (void) ca_path;
  (void) ca_file;
  return x_error(X_FAILURE, ENOSYS, fn, "smax-clib was built without TLS support");
#endif
}

/**
 * Disables a previously enabled TLS configuration for SMA-X.
 *
 * @return   X_SUCCESS (0) if successful, or else X_FAILURE (-1) if the SMA-X library was built without TLS
 *           support.
 *
 * @sa smaxSetTLS()
 */
int smaxDisableTLS() {
#if WITH_TLS
  smaxLockConfig();
  config.enabled = FALSE;
  smaxUnlockConfig();
#endif
  return X_SUCCESS;
}

/**
 * Sets whether to verify the the certificate. Certificates are verified by default.
 *
 * @param value   TRUE (non-zero) to verify certificates, or else FALSE (0).
 * @return        X_SUCCESS (0) if successful, or else X_FAILURE (-1) if the SMA-X library was built without TLS
 *                support.
 *
 * @sa smaxSetTLS()
 */
int smaxSetTLSVerify(boolean value) {
#if WITH_TLS
  smaxLockConfig();
  config.skip_verify = !value;
  smaxUnlockConfig();
  return X_SUCCESS;
#else
  (void) value;
  return x_error(X_FAILURE, ENOSYS, "smaxSetTLSVerify", "smax-clib was built without TLS support");
#endif
}

/**
 * Set a TLS certificate and private key for mutual TLS. You will still need to call smaxSetTLS() also to create a
 * complete TLS configuration. Redis normally uses mutual TLS, which requires both the client and the server to
 * authenticate themselves. For this you need the server's TLS certificate and private key also. It is possible to
 * configure Redis servers to verify one way only with a CA certificate, in which case you don't need to call this to
 * configure the client.
 *
 * To disable mutual TLS, set both file name arguments to NULL.
 *
 * @param cert_file   Path to the server's certificate file.
 * @param key_file    Path to the server'sprivate key file.
 * @return            X_SUCCESS (0) if successful, or, X_NAME_INVALID if the certificate or private key file is not
 *                    accessible, or else X_FAILURE (-1) if the SMA-X library was built without TLS support.
 *
 * @sa smaxSetTLS()
 */
int smaxSetMutualTLS(const char *cert_file, const char *key_file) {
  static const char *fn = "smaxSetMutualTLS";

#if WITH_TLS
  if(cert_file) if(access(cert_file, R_OK) != 0) return x_error(X_NAME_INVALID, EINVAL, fn, "certificate not accessible: %s", cert_file);
  if(key_file) if(access(key_file, R_OK) != 0) return x_error(X_NAME_INVALID, EINVAL, fn, "private key not accessible: %s", key_file);

  smaxLockConfig();
  if(config.certificate) free(config.certificate);
  config.certificate = xStringCopyOf(cert_file);
  if(config.key) free(config.key);
  config.key = xStringCopyOf(key_file);
  smaxUnlockConfig();
  return X_SUCCESS;
#else
  (void) cert_file;
  (void) key_file;
  return x_error(X_FAILURE, ENOSYS, fn, "smax-clib was built without TLS support");
#endif
}

/**
 * Sets the Server name for TLS Server Name Indication (SNI), an optional extra later of security.
 *
 * @param host    server name to use for SNI.
 * @return        X_SUCCESS (0) if successful, or else X_FAILURE (-1) if the SMA-X library was built without TLS
 *                support.
 *
 * @sa smaxSetTLS()
 */
int smaxSetTLSServerName(const char *host) {
#if WITH_TLS
  smaxLockConfig();
  if(config.hostname) free(config.hostname);
  config.hostname = xStringCopyOf(host);
  smaxUnlockConfig();
  return X_SUCCESS;
#else
  (void) host;
  return x_error(X_FAILURE, ENOSYS, "smaxSetTLSServerName", "smax-clib was built without TLS support");
#endif
}

/**
 * Sets the TLS ciphers to try (TLSv1.2 and earlier).
 *
 * @param list      a colon (:) separated list of ciphers, or NULL for default ciphers.
 * @return          X_SUCCESS (0) if successful, or else X_FAILURE (-1) if the SMA-X library was built
 *                  without TLS support.
 *
 * @sa smaxSetTLSCipherSuites()
 * @sa smaxSetTLS()
 * @sa smaSetDHCipherParams()
 */
int smaxSetTLSCiphers(const char *list) {
#if WITH_TLS
  smaxLockConfig();
  if(config.ciphers) free(config.ciphers);
  config.ciphers = xStringCopyOf(list);
  smaxUnlockConfig();
  return X_SUCCESS;
#else
  (void) list;
  return x_error(X_FAILURE, ENOSYS, "smaxSetTLSCiphers", "smax-clib was built without TLS support");
#endif
}

/**
 * Sets the TLS ciphers suites to try (TLSv1.3 and later).
 *
 * @param list             a colon (:) separated list of cipher suites, or NULL for default cipher suites.
 * @return                 X_SUCCESS (0) if successful, or else X_FAILURE (-1) if the SMA-X library was built
 *                         without TLS support.
 *
 * @sa smaxSetTLSCiphers()
 * @sa smaxSetTLS()
 * @sa smaxSetDHCipherParams()
 */
int smaxSetTLSCipherSuites(const char *list) {
#if WITH_TLS
  smaxLockConfig();
  if(config.cipher_suites) free(config.cipher_suites);
  config.cipher_suites = xStringCopyOf(list);
  smaxUnlockConfig();
  return X_SUCCESS;
#else
  (void) list;
  return x_error(X_FAILURE, ENOSYS, "smaxSetTLSCipherSuites", "smax-clib was built without TLS support");
#endif
}

/**
 * Sets parameters for DH-based cyphers when using a TLS encrypted connection.
 *
 * @param dh_file     Path to the DH-based cypher parameters file (in PEM format; we don't support
 *                    the old DER format), or NULL for no params.
 * @return            X_SUCCESS (0) if successful, or X_NAME_INVALID if the file is not accessible, or else
 *                    X_FAILURE (-1) if the SMA-X library was built without TLS support.
 *
 * @sa smaxSetTLS()
 * @sa smaxSetTLSCiphers()
 * @sa smaxSetTLSCipherSuites()
 */
int smaxSetDHCipherParams(const char *dh_file) {
#if WITH_TLS
  if(dh_file) if(access(dh_file, R_OK) != 0) return x_error(X_NAME_INVALID, EINVAL, "smaxSetDHCipherParams", "DH parameters not accessible: %s", dh_file);

  smaxLockConfig();
  if(config.dh_params) free(config.dh_params);
  config.dh_params = xStringCopyOf(dh_file);
  smaxUnlockConfig();
  return X_SUCCESS;
#else
  (void) dh_file;
  return x_error(X_FAILURE, ENOSYS, "smaxSetDHCipherParams", "smax-clib was built without TLS support");
#endif
}
