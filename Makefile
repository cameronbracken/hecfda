PYTHON ?= python3
# quartodoc must run under a python that can import hecfdapy (it reads the installed package's
# docstrings). Override on the command line, e.g. `make docs QUARTODOC=~/venv/hecfdapy/bin/quartodoc`,
# if the quartodoc on PATH is not the one tied to $(PYTHON).
QUARTODOC ?= quartodoc

.PHONY: test-core test-r test-py materialize oracles build-r build-py docs docs-serve

test-core:
	cmake -S core -B core/build && cmake --build core/build && ctest --test-dir core/build --output-on-failure

test-r:
	Rscript -e 'cpp11::cpp_register("hecfdar")'
	R CMD INSTALL --preclean hecfdar
	Rscript -e 'testthat::test_local("hecfdar")'

test-py:
	$(PYTHON) -m pip install --force-reinstall --no-deps ./hecfdapy
	$(PYTHON) -m pytest hecfdapy/tests -q

materialize:
	$(PYTHON) tools/materialize_core.py

oracles:
	$(PYTHON) tools/verify_oracles.py

build-r:
	Rscript -e 'cpp11::cpp_register("hecfdar")'
	Rscript -e 'roxygen2::roxygenise("hecfdar")'
	R CMD build hecfdar

build-py:
	$(PYTHON) -m build hecfdapy

docs:
	cd site && $(QUARTODOC) build
	quarto render site
	Rscript -e 'pkgdown::build_site("hecfdar", preview = FALSE)'
	mkdir -p site/_site/r
	cp -R hecfdar/docs/. site/_site/r/
	touch site/_site/.nojekyll

# Serve the ASSEMBLED site (quarto preview will not serve the pkgdown half at /r/).
docs-serve:
	$(PYTHON) -m http.server -d site/_site 8000
