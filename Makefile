PYTHON ?= python3

.PHONY: test-core test-r test-py materialize oracles

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
	python3 tools/materialize_core.py

oracles:
	python3 tools/verify_oracles.py
