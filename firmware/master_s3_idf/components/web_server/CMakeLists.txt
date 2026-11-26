idf_component_register(SRCS "src/web_server.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_http_server esp_https_server json log mbedtls
                    EMBED_TXTFILES "certs/servercert.pem"
                                   "certs/prvtkey.pem")