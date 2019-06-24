# SPDX-License-Identifier: BSD-3-Clause

cmocka_test(multidma
	multidma_test.c
	mock_dma.c
	stub.c
	${PROJECT_SOURCE_DIR}/src/drivers/intel/cavs/multidma.c
	${PROJECT_SOURCE_DIR}/src/audio/channel_map.c
)
# target_link_libraries(mixer PRIVATE -lm)
