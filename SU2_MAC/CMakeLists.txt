ADD_EXECUTABLE ( SU2_MAC
  src/SU2_MAC.cpp
  )

TARGET_LINK_LIBRARIES ( SU2_MAC
  SU2
  ${JSONCPP_json_LIBRARY}
  ${CGNS_cgns_LIBRARY}
  ${Metis_metis_LIBRARY}
  ${MPI_EXTRA_LIBRARY}
  ${MPI_LIBRARY}
  )

INSTALL ( TARGETS
  SU2_MAC
  DESTINATION
  bin
  )
