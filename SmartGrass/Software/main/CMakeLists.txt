idf_component_register(SRCS "main.c"
                    REQUIRES
                        esp_timer
                        a4988
                        i2c
                        mks_servo42c
                        mpu6050
                        server
                        soft_ap
                        uart
                        vehicle
                        brushless
                    INCLUDE_DIRS ".")