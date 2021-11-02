set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${NO_STRICT_ALIASING_FLAGS}")

rdma_shared_provider(mana libmana.map
  1 1.0.${PACKAGE_VERSION}
  mana.c
  mana_dv.c
  qp.c
  wq.c
)

publish_headers(infiniband
  mana_dv.h
)

