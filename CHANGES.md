Changes 0.6.2
- enable safety check guaranteeing that module definition did not change between test vector request and test response submission
- fix bug in acvp_publish
- SIGSEGV is caught to remove message queue
- OpenSSL: add missing CBC
- statically link JSON-C
- fix make cppcheck
- compile with -Wno-missing-field-initializers as some compilers require all structure fields to be initialized
- re-enable ACVP cancel operation upon SIGTERM, SIGINT, SIGQUIT, SIGHUP
- allow ACVP cancel operation to be terminated with 2nd receipt of signals

Changes 0.6.1
- Support --vsid without --testid on command line
- MQ server: fix starting and restarting of server thread
- fix IKEv2 register operation

Changes 0.6.0
 * first public release with support for ACVP v0.5
