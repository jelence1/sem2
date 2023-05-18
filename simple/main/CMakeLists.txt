idf_component_register(SRCS "main.c" "dht.c"
                    INCLUDE_DIRS "." "inc"
                    EMBED_TXTFILES "certs/ClientCert.pem"
                                   "certs/ServerCert.pem"
                                   "certs/ServerKey.pem")
