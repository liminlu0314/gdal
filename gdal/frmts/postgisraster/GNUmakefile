# USER CONFIGURATION
# END OF USER CONFIGURATION

include ../../GDALmake.opt

OBJ	=	postgisrasterdriver.o postgisrasterdataset.o postgisrasterrasterband.o postgisrastertiledataset.o postgisrastertilerasterband.o postgisrastertools.o


CPPFLAGS	:= -I ../mem -I ../vrt $(XTRA_OPT) $(PG_INC)  $(CPPFLAGS)

CXXFLAGS        :=      $(WARN_EFFCPLUSPLUS) $(WARN_OLD_STYLE_CAST) $(CXXFLAGS)

PLUGIN_SO =	gdal_PostGISRaster.so

default:	$(OBJ:.o=.$(OBJ_EXT))

$(O_OBJ):       postgisraster.h ../vrt/vrtdataset.h ../mem/memdataset.h


plugin: $(PLUGIN_SO)

$(PLUGIN_SO):	$(OBJ)
	$(LD_SHARED) $(LNK_FLAGS) $(OBJ) $(CONFIG_LIBS) $(EXTRA_LIBS) \
		-o $(PLUGIN_SO)

clean:
	rm -f *.o $(O_OBJ) *.so

../o/%.$(OBJ_EXT):
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@

all:	$(OBJ:.o=.$(OBJ_EXT))

install-obj:	$(O_OBJ:.o=.$(OBJ_EXT))
