idf_component_register(SRCS "vehicle.c"
                    REQUIRES
                        a4988
                        mks_servo42c
                        brushless
                    INCLUDE_DIRS "include")
