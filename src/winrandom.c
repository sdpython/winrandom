/*
winrandom.c

Python interface to Windows Cryptographic API CryptGenRandom()

Pawel Krawczyk <pawel.krawczyk@hush.com>
*/

#include <Python.h>

/////////////////////////////////////////////////////
// from https://docs.python.org/3/howto/cporting.html
/////////////////////////////////////////////////////

struct module_state {
    PyObject *error;
};

#if PY_MAJOR_VERSION >= 3
#define GETSTATE(m) ((struct module_state*)PyModule_GetState(m))
#else
#define GETSTATE(m) (&_state)
static struct module_state _state;
#endif

static PyObject *
error_out(PyObject *m, const char * message) {
    struct module_state *st = GETSTATE(m);
    PyErr_SetString(st->error, message);
    return NULL;
}

#if PY_MAJOR_VERSION >= 3

static int winrandom_traverse(PyObject *m, visitproc visit, void *arg) {
    Py_VISIT(GETSTATE(m)->error);
    return 0;
}

static int winrandom_clear(PyObject *m) {
    Py_CLEAR(GETSTATE(m)->error);
    return 0;
}

/////////////////////////////////////////////////////

#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <math.h>
#include <stdio.h>
#include <ctype.h>


/* This function implements B.5.1.2 The Complex Discard Method from NIST SP800-90
 * http://csrc.nist.gov/publications/nistpubs/800-90A/SP800-90A.pdf
 * Discard method is used to produce random LONG until it fits in 0..MAX range
 * Variables are named using the NIST document convention (r, c etc).
 */
static PyObject *winrandom_range(PyObject *self, PyObject *args) {
	HCRYPTPROV hProv;
	unsigned long c;	// output random number
	static unsigned long iContinousRndTest = 0L;
	unsigned long t;
	double upperLimitBits; /* because ceil() returns DOUBLE */
	unsigned long r; // upper limit, unsigned so we can catch negative arguments
	int ok;

	ok = PyArg_ParseTuple(args, "l", &r);
	if(!ok) {
        PyErr_SetString(PyExc_ValueError, "unable to parse arguments");
		return NULL;
	}
	if(r <= 1) {
		return error_out(self, "rand_max needs to be >1 because for 1 the upperLimitBits will be 0 and no random number"
                               "  will be returned; the logic of this function is that 0 <= n <= rand_max-1)");
	}

	/* try stronger crypto provider first */
	ok = CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT);
	if(!ok) {
		ok = CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
		if(!ok) {
			return error_out(self, "Unable to acquire Windows random number generator");
		}
	}

	// how many bits are needed to store max
	// need to use log(2) as log() is base e
	upperLimitBits = ceil(log(r-1) / log(2));
	t = (long) ceil(upperLimitBits/8); // how many bytes

	/* Fetch random bytes until it's lower than desired range */
	while(1) {
		long retVal;
		c = 0L; /* overwrite whatever was there */

		/*
		 * This implementation of the discard is operating on bytes (8 bit blocks) and
		 * not single bytes, like the NIST version. In practice this means that we need to execute
		 * more loops to find the right value (c < rand_max). The farther rand_max is
		 * from byte boundary, the more checks we need to perform.
		 */
		retVal = CryptGenRandom(hProv, t, (BYTE *) &c);
		if(!retVal) {
			return error_out(self, "Unable to fetch random data from Windows");
		}
		/* FIPS 140-2 p. 44 Continuous random number generator test */
		/* Check if previous number wasn't the same as current */
		if(upperLimitBits > 15 && c == iContinousRndTest) {
            return error_out(self, "Continuous random number generator test failed");
        }
		iContinousRndTest = c; /* preserve this value for continuous test */
		if(c < (unsigned long) r) break; // found!
	}

	CryptReleaseContext(hProv, 0);
	return Py_BuildValue("k", c);
}


static PyObject *winrandom_bytes(PyObject *self, PyObject *args)
{
	HCRYPTPROV hProv;
	unsigned int num_bytes;
	unsigned char *s;
	int ok;

	ok = PyArg_ParseTuple(args, "I", &num_bytes);
	if(!ok) {
        PyErr_SetString(PyExc_ValueError, "unable to parse arguments");
		return NULL;
	}

	s = malloc(num_bytes);
	if(s == NULL) {
        PyErr_SetString(PyExc_MemoryError, "Memory issue");
		return NULL;
	}

	// CryptoAPI
	/* try stronger crypto provider first */
    ok = CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT);
    if(!ok) {
        ok = CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
        if(!ok) {
            return error_out(self, "Unable to acquire Windows random number generator");
        }
    }

	ok = CryptGenRandom(hProv, (DWORD) num_bytes, (BYTE *) s);
	if(!ok) {
        return error_out(self, "Unable to fetch random data from Windows");
    }

	return Py_BuildValue("y#", s, num_bytes);
}


static PyObject *winrandom_long(PyObject *self, PyObject *args)
{
	HCRYPTPROV hProv;
	unsigned long pbRandomData;
	int ok;

	/* try stronger crypto provider first */
    ok = CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT);
    if(!ok) {
        ok = CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
        if(!ok) {
            return error_out(self, "Unable to acquire Windows random number generator");
		}
	}

	//  Generate eight bytes of random data into pbRandomData.
	ok = CryptGenRandom(hProv, (DWORD) sizeof(pbRandomData), (BYTE *) &pbRandomData);
	if(!ok) {
        return error_out(self, "Unable to fetch random data from Windows");
	}

	return Py_BuildValue("k", pbRandomData);
}


#define LONG_TEXT 	"winrandom.long() - get cryptographically strong pseudo-random long integer."
#define BYTES_TEXT 	"winrandom.bytes(N) - get N cryptographically strong pseudo-random bytes."
#define RANGE_TEXT 	"winrandom.range(MAX) - get cryptographically strong pseudo-random integer N that is 0 <= N < MAX." \
					"Note that the returned is between 0 and MAX-1 inclusive. To cycle between 0 and 1 you need range(2)."

static PyMethodDef WinrandomMethods[] = {
	{"long", winrandom_long, METH_VARARGS, LONG_TEXT },
	{"bytes", winrandom_bytes, METH_VARARGS, BYTES_TEXT },
	{"range", winrandom_range, METH_VARARGS, RANGE_TEXT },
	{NULL, NULL, 0, NULL}
};

/*
PyMODINIT_FUNC
initwinrandom(void) {
	PyObject *m;

	 m = Py_InitModule("winrandom", WinrandomMethods);
	 if (m == NULL)
		return;

	 exception = PyErr_NewException("winrandom.error", NULL, NULL);
	 Py_INCREF(exception);
	 PyModule_AddObject(m, "error", exception);

	 //PyDict_SetItemString(d, "error", exception);
}
*/

/////////////////////////////////////////////////////
// from https://docs.python.org/3/howto/cporting.html
/////////////////////////////////////////////////////


static struct PyModuleDef moduledef = {
        PyModuleDef_HEAD_INIT,
        "winrandom",
        NULL,
        sizeof(struct module_state),
        WinrandomMethods,
        NULL,
        winrandom_traverse,
        winrandom_clear,
        NULL
};

#define INITERROR return NULL

PyMODINIT_FUNC
PyInit_winrandom(void)

#else
#define INITERROR return

void
initwinrandom(void)
#endif
{
#if PY_MAJOR_VERSION >= 3
    PyObject *module = PyModule_Create(&moduledef);
#else
    PyObject *module = Py_InitModule("winrandom", winrandom_methods);
#endif

    if (module == NULL)
        INITERROR;
    struct module_state *st = GETSTATE(module);

    st->error = PyErr_NewException("winrandom.WinRandomException", NULL, NULL);
    if (st->error == NULL) {
        Py_DECREF(module);
        INITERROR;
    }

#if PY_MAJOR_VERSION >= 3
    return module;
#endif
}


