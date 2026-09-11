# Framing for the sidekick/main link (I28a, RFC 0004 §6).
#
# No REQUIRES: the codec is deliberately dependency-free -- no ESP-IDF, no
# driver, nothing transport-specific. That is what lets the sidekick project
# compile the same file (RFC 0002 §5.1's "more than one idf.py project"
# exception) and what keeps B47's UART-versus-I2C question open.
idf_component_register(
    SRCS "porta_proto.c"
    INCLUDE_DIRS "include"
)
