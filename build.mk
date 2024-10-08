
# ============================================================================
# Generic build targets and recipes for xchange.
# 
# You can include this in your Makefile also.
# ============================================================================


# Regular object files
$(OBJ)/%.o: %.c dep/%.d $(OBJ) Makefile
	$(CC) -o $@ -c $(CPPFLAGS) $(CFLAGS) $<

# Share librarry recipe
$(LIB)/%.so.$(SO_VERSION) : | $(LIB) Makefile
	$(CC) -o $@ $(CPPFLAGS) $(CFLAGS) $^ -shared -fPIC -Wl,-soname,$(subst $(LIB)/,,$@) $(LDFLAGS)

# Unversioned shared libs (for linking against)
$(LIB)/lib%.so:
	@rm -f $@
	ln -sr $< $@

# Static library: *.a
$(LIB)/%.a: | $(LIB) Makefile
	ar -rc $@ $^
	ranlib $@

# Simple binaries
$(BIN)/%: $(OBJ)/%.o | $(BIN)
	$(CC) -o $@ $^ $(LDFLAGS)

# Create sub-directories for build targets
dep $(OBJ) $(LIB) $(BIN) apidoc:
	mkdir $@

# Remove intermediate files locally
.PHONY: clean-local
clean-local:
	rm -rf obj dep

# Remove all locally built files, effectively restoring the repo to its 
# pristine state
.PHONY: distclean-local
distclean-local: clean-local
	rm -rf bin lib apidoc

# Remove intermediate files (general)
.PHONY: clean
clean: clean-local

# Remove intermediate files (general)
.PHONY: distclean
distclean: distclean-local

# Static code analysis using 'cppcheck'
.PHONY: check
check:
	@echo "   [check]"
	@cppcheck $(CPPFLAGS) $(CHECKOPTS) src

# Doxygen documentation (HTML and man pages) under apidocs/
.PHONY: dox
dox: README.md Doxyfile | apidoc $(SRC) $(INC)
	@echo "   [doxygen]"
	@$(DOXYGEN)

# Automatic dependence on included header files.
.PRECIOUS: dep/%.d
dep/%.d: $(SRC)/%.c | dep
	@echo " > $@" \
	&& $(CC) $(CPPFLAGS) -MM -MG $< > $@.$$$$ \
	&& sed 's|\w*\.o[ :]*| $(OBJ)/&|g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

