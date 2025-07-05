#!/usr/bin/env python

# tests taken and modified from https://github.com/python/cpython/blob/main/Lib/test/test_urlparse.py
# for some reason a lot of tests failed for python urllib itself, so they were commented out

# incompatible tests were commented out with lines beginning with "# !!"

import sys

import subprocess

# import urllib.parse

RFC1808_BASE = "http://a/b/c/d;p?q#f"
RFC2396_BASE = "http://a/b/c/d;p?q"
RFC3986_BASE = "http://a/b/c/d;p?q"
SIMPLE_BASE = "http://a/b/c/d"


class UrlParseTestCase:
    def checkJoin(self, base, relurl, expected):
        x = (
            subprocess.run(
                ["./reliq", "--urljoin", base, relurl], stdout=subprocess.PIPE
            )
            .stdout.decode("utf-8")
            .split("\n", 1)[0]
        )

        assert x == expected
        # assert urllib.parse.urljoin(base, relurl) == expected

    def test(self):
        self.test_RFC1808()

        self.test_RFC2396()

        self.test_RFC3986()

        self.test_urljoins()

        self.test_urljoins_relative_base()

    def test_RFC1808(self):
        # "normal" cases from RFC 1808:
        self.checkJoin(RFC1808_BASE, "g:h", "g:h")
        self.checkJoin(RFC1808_BASE, "g", "http://a/b/c/g")
        self.checkJoin(RFC1808_BASE, "./g", "http://a/b/c/g")
        self.checkJoin(RFC1808_BASE, "g/", "http://a/b/c/g/")
        self.checkJoin(RFC1808_BASE, "/g", "http://a/g")
        self.checkJoin(RFC1808_BASE, "//g", "http://g")
        self.checkJoin(RFC1808_BASE, "g?y", "http://a/b/c/g?y")
        self.checkJoin(RFC1808_BASE, "g?y/./x", "http://a/b/c/g?y/./x")
        self.checkJoin(RFC1808_BASE, "#s", "http://a/b/c/d;p?q#s")
        self.checkJoin(RFC1808_BASE, "g#s", "http://a/b/c/g#s")
        self.checkJoin(RFC1808_BASE, "g#s/./x", "http://a/b/c/g#s/./x")
        self.checkJoin(RFC1808_BASE, "g?y#s", "http://a/b/c/g?y#s")
        self.checkJoin(RFC1808_BASE, "g;x", "http://a/b/c/g;x")
        self.checkJoin(RFC1808_BASE, "g;x?y#s", "http://a/b/c/g;x?y#s")
        self.checkJoin(RFC1808_BASE, ".", "http://a/b/c/")
        self.checkJoin(RFC1808_BASE, "./", "http://a/b/c/")
        self.checkJoin(RFC1808_BASE, "..", "http://a/b/")
        self.checkJoin(RFC1808_BASE, "../", "http://a/b/")
        self.checkJoin(RFC1808_BASE, "../g", "http://a/b/g")
        self.checkJoin(RFC1808_BASE, "../..", "http://a/")
        self.checkJoin(RFC1808_BASE, "../../", "http://a/")
        self.checkJoin(RFC1808_BASE, "../../g", "http://a/g")

        # "abnormal" cases from RFC 1808:
        self.checkJoin(RFC1808_BASE, "", "http://a/b/c/d;p?q#f")
        self.checkJoin(RFC1808_BASE, "g.", "http://a/b/c/g.")
        self.checkJoin(RFC1808_BASE, ".g", "http://a/b/c/.g")
        self.checkJoin(RFC1808_BASE, "g..", "http://a/b/c/g..")
        self.checkJoin(RFC1808_BASE, "..g", "http://a/b/c/..g")
        self.checkJoin(RFC1808_BASE, "./../g", "http://a/b/g")
        self.checkJoin(RFC1808_BASE, "./g/.", "http://a/b/c/g/")
        self.checkJoin(RFC1808_BASE, "g/./h", "http://a/b/c/g/h")
        self.checkJoin(RFC1808_BASE, "g/../h", "http://a/b/c/h")

        # RFC 1808 and RFC 1630 disagree on these (according to RFC 1808),
        # so we'll not actually run these tests (which expect 1808 behavior).
        # self.checkJoin(RFC1808_BASE, 'http:g', 'http:g')
        # self.checkJoin(RFC1808_BASE, 'http:', 'http:')

        # XXX: The following tests are no longer compatible with RFC3986
        # self.checkJoin(RFC1808_BASE, '../../../g', 'http://a/../g')
        # self.checkJoin(RFC1808_BASE, '../../../../g', 'http://a/../../g')
        # self.checkJoin(RFC1808_BASE, '/./g', 'http://a/./g')
        # self.checkJoin(RFC1808_BASE, '/../g', 'http://a/../g')

    def test_RFC2396(self):
        # cases from RFC 2396

        self.checkJoin(RFC2396_BASE, "g:h", "g:h")
        self.checkJoin(RFC2396_BASE, "g", "http://a/b/c/g")
        self.checkJoin(RFC2396_BASE, "./g", "http://a/b/c/g")
        self.checkJoin(RFC2396_BASE, "g/", "http://a/b/c/g/")
        self.checkJoin(RFC2396_BASE, "/g", "http://a/g")
        self.checkJoin(RFC2396_BASE, "//g", "http://g")
        self.checkJoin(RFC2396_BASE, "g?y", "http://a/b/c/g?y")
        self.checkJoin(RFC2396_BASE, "#s", "http://a/b/c/d;p?q#s")
        self.checkJoin(RFC2396_BASE, "g#s", "http://a/b/c/g#s")
        self.checkJoin(RFC2396_BASE, "g?y#s", "http://a/b/c/g?y#s")
        self.checkJoin(RFC2396_BASE, "g;x", "http://a/b/c/g;x")
        self.checkJoin(RFC2396_BASE, "g;x?y#s", "http://a/b/c/g;x?y#s")
        self.checkJoin(RFC2396_BASE, ".", "http://a/b/c/")
        self.checkJoin(RFC2396_BASE, "./", "http://a/b/c/")
        self.checkJoin(RFC2396_BASE, "..", "http://a/b/")
        self.checkJoin(RFC2396_BASE, "../", "http://a/b/")
        self.checkJoin(RFC2396_BASE, "../g", "http://a/b/g")
        self.checkJoin(RFC2396_BASE, "../..", "http://a/")
        self.checkJoin(RFC2396_BASE, "../../", "http://a/")
        self.checkJoin(RFC2396_BASE, "../../g", "http://a/g")
        self.checkJoin(RFC2396_BASE, "", RFC2396_BASE)
        self.checkJoin(RFC2396_BASE, "g.", "http://a/b/c/g.")
        self.checkJoin(RFC2396_BASE, ".g", "http://a/b/c/.g")
        self.checkJoin(RFC2396_BASE, "g..", "http://a/b/c/g..")
        self.checkJoin(RFC2396_BASE, "..g", "http://a/b/c/..g")
        self.checkJoin(RFC2396_BASE, "./../g", "http://a/b/g")
        self.checkJoin(RFC2396_BASE, "./g/.", "http://a/b/c/g/")
        self.checkJoin(RFC2396_BASE, "g/./h", "http://a/b/c/g/h")
        self.checkJoin(RFC2396_BASE, "g/../h", "http://a/b/c/h")
        self.checkJoin(RFC2396_BASE, "g;x=1/./y", "http://a/b/c/g;x=1/y")
        self.checkJoin(RFC2396_BASE, "g;x=1/../y", "http://a/b/c/y")
        self.checkJoin(RFC2396_BASE, "g?y/./x", "http://a/b/c/g?y/./x")
        self.checkJoin(RFC2396_BASE, "g?y/../x", "http://a/b/c/g?y/../x")
        self.checkJoin(RFC2396_BASE, "g#s/./x", "http://a/b/c/g#s/./x")
        self.checkJoin(RFC2396_BASE, "g#s/../x", "http://a/b/c/g#s/../x")

        # XXX: The following tests are no longer compatible with RFC3986
        # self.checkJoin(RFC2396_BASE, '../../../g', 'http://a/../g')
        # self.checkJoin(RFC2396_BASE, '../../../../g', 'http://a/../../g')
        # self.checkJoin(RFC2396_BASE, '/./g', 'http://a/./g')
        # self.checkJoin(RFC2396_BASE, '/../g', 'http://a/../g')

    def test_RFC3986(self):
        self.checkJoin(RFC3986_BASE, "?y", "http://a/b/c/d;p?y")
        self.checkJoin(RFC3986_BASE, ";x", "http://a/b/c/;x")
        self.checkJoin(RFC3986_BASE, "g:h", "g:h")
        self.checkJoin(RFC3986_BASE, "g", "http://a/b/c/g")
        self.checkJoin(RFC3986_BASE, "./g", "http://a/b/c/g")
        self.checkJoin(RFC3986_BASE, "g/", "http://a/b/c/g/")
        self.checkJoin(RFC3986_BASE, "/g", "http://a/g")
        self.checkJoin(RFC3986_BASE, "//g", "http://g")
        self.checkJoin(RFC3986_BASE, "?y", "http://a/b/c/d;p?y")
        self.checkJoin(RFC3986_BASE, "g?y", "http://a/b/c/g?y")
        self.checkJoin(RFC3986_BASE, "#s", "http://a/b/c/d;p?q#s")
        self.checkJoin(RFC3986_BASE, "g#s", "http://a/b/c/g#s")
        self.checkJoin(RFC3986_BASE, "g?y#s", "http://a/b/c/g?y#s")
        self.checkJoin(RFC3986_BASE, ";x", "http://a/b/c/;x")
        self.checkJoin(RFC3986_BASE, "g;x", "http://a/b/c/g;x")
        self.checkJoin(RFC3986_BASE, "g;x?y#s", "http://a/b/c/g;x?y#s")
        self.checkJoin(RFC3986_BASE, "", "http://a/b/c/d;p?q")
        self.checkJoin(RFC3986_BASE, ".", "http://a/b/c/")
        self.checkJoin(RFC3986_BASE, "./", "http://a/b/c/")
        self.checkJoin(RFC3986_BASE, "..", "http://a/b/")
        self.checkJoin(RFC3986_BASE, "../", "http://a/b/")
        self.checkJoin(RFC3986_BASE, "../g", "http://a/b/g")
        self.checkJoin(RFC3986_BASE, "../..", "http://a/")
        self.checkJoin(RFC3986_BASE, "../../", "http://a/")
        self.checkJoin(RFC3986_BASE, "../../g", "http://a/g")
        self.checkJoin(RFC3986_BASE, "../../../g", "http://a/g")

        # Abnormal Examples

        # The 'abnormal scenarios' are incompatible with RFC2986 parsing
        # Tests are here for reference.

        self.checkJoin(RFC3986_BASE, "../../../g", "http://a/g")
        self.checkJoin(RFC3986_BASE, "../../../../g", "http://a/g")
        self.checkJoin(RFC3986_BASE, "/./g", "http://a/g")
        self.checkJoin(RFC3986_BASE, "/../g", "http://a/g")
        self.checkJoin(RFC3986_BASE, "g.", "http://a/b/c/g.")
        self.checkJoin(RFC3986_BASE, ".g", "http://a/b/c/.g")
        self.checkJoin(RFC3986_BASE, "g..", "http://a/b/c/g..")
        self.checkJoin(RFC3986_BASE, "..g", "http://a/b/c/..g")
        self.checkJoin(RFC3986_BASE, "./../g", "http://a/b/g")
        self.checkJoin(RFC3986_BASE, "./g/.", "http://a/b/c/g/")
        self.checkJoin(RFC3986_BASE, "g/./h", "http://a/b/c/g/h")
        self.checkJoin(RFC3986_BASE, "g/../h", "http://a/b/c/h")
        self.checkJoin(RFC3986_BASE, "g;x=1/./y", "http://a/b/c/g;x=1/y")
        self.checkJoin(RFC3986_BASE, "g;x=1/../y", "http://a/b/c/y")
        self.checkJoin(RFC3986_BASE, "g?y/./x", "http://a/b/c/g?y/./x")
        self.checkJoin(RFC3986_BASE, "g?y/../x", "http://a/b/c/g?y/../x")
        self.checkJoin(RFC3986_BASE, "g#s/./x", "http://a/b/c/g#s/./x")
        self.checkJoin(RFC3986_BASE, "g#s/../x", "http://a/b/c/g#s/../x")
        # self.checkJoin(RFC3986_BASE, 'http:g','http:g') # strict parser
        self.checkJoin(RFC3986_BASE, "http:g", "http://a/b/c/g")  # relaxed parser

        # Test for issue9721
        self.checkJoin("http://a/b/c/de", ";x", "http://a/b/c/;x")

    def test_urljoins(self):
        self.checkJoin(SIMPLE_BASE, "g:h", "g:h")
        self.checkJoin(SIMPLE_BASE, "g", "http://a/b/c/g")
        self.checkJoin(SIMPLE_BASE, "./g", "http://a/b/c/g")
        self.checkJoin(SIMPLE_BASE, "g/", "http://a/b/c/g/")
        self.checkJoin(SIMPLE_BASE, "/g", "http://a/g")
        self.checkJoin(SIMPLE_BASE, "//g", "http://g")
        self.checkJoin(SIMPLE_BASE, "?y", "http://a/b/c/d?y")
        self.checkJoin(SIMPLE_BASE, "g?y", "http://a/b/c/g?y")
        self.checkJoin(SIMPLE_BASE, "g?y/./x", "http://a/b/c/g?y/./x")
        self.checkJoin(SIMPLE_BASE, ".", "http://a/b/c/")
        self.checkJoin(SIMPLE_BASE, "./", "http://a/b/c/")
        self.checkJoin(SIMPLE_BASE, "..", "http://a/b/")
        self.checkJoin(SIMPLE_BASE, "../", "http://a/b/")
        self.checkJoin(SIMPLE_BASE, "../g", "http://a/b/g")
        self.checkJoin(SIMPLE_BASE, "../..", "http://a/")
        self.checkJoin(SIMPLE_BASE, "../../g", "http://a/g")
        self.checkJoin(SIMPLE_BASE, "./../g", "http://a/b/g")
        self.checkJoin(SIMPLE_BASE, "./g/.", "http://a/b/c/g/")
        self.checkJoin(SIMPLE_BASE, "g/./h", "http://a/b/c/g/h")
        self.checkJoin(SIMPLE_BASE, "g/../h", "http://a/b/c/h")
        self.checkJoin(SIMPLE_BASE, "http:g", "http://a/b/c/g")
        self.checkJoin(SIMPLE_BASE, "http:g?y", "http://a/b/c/g?y")
        self.checkJoin(SIMPLE_BASE, "http:g?y/./x", "http://a/b/c/g?y/./x")
        self.checkJoin("http:///", "..", "http:///")
        self.checkJoin("", "http://a/b/c/g?y/./x", "http://a/b/c/g?y/./x")
        self.checkJoin("", "http://a/./g", "http://a/./g")
        self.checkJoin("svn://pathtorepo/dir1", "dir2", "svn://pathtorepo/dir2")
        self.checkJoin("svn+ssh://pathtorepo/dir1", "dir2", "svn+ssh://pathtorepo/dir2")
        self.checkJoin("ws://a/b", "g", "ws://a/g")
        self.checkJoin("wss://a/b", "g", "wss://a/g")

        # XXX: The following tests are no longer compatible with RFC3986
        # self.checkJoin(SIMPLE_BASE, '../../../g','http://a/../g')
        # self.checkJoin(SIMPLE_BASE, '/./g','http://a/./g')

        # test for issue22118 duplicate slashes
        self.checkJoin(SIMPLE_BASE + "/", "foo", SIMPLE_BASE + "/foo")

        # Non-RFC-defined tests, covering variations of base and trailing
        # slashes
        self.checkJoin("http://a/b/c/d/e/", "../../f/g/", "http://a/b/c/f/g/")
        self.checkJoin("http://a/b/c/d/e", "../../f/g/", "http://a/b/f/g/")
        self.checkJoin("http://a/b/c/d/e/", "/../../f/g/", "http://a/f/g/")
        self.checkJoin("http://a/b/c/d/e", "/../../f/g/", "http://a/f/g/")
        self.checkJoin("http://a/b/c/d/e/", "../../f/g", "http://a/b/c/f/g")
        self.checkJoin("http://a/b/", "../../f/g/", "http://a/f/g/")

        # issue 23703: don't duplicate filename
        self.checkJoin("a", "b", "b")

        # Test with empty (but defined) components.
        self.checkJoin(RFC1808_BASE, "", "http://a/b/c/d;p?q#f")
        # self.checkJoin(RFC1808_BASE, "#", "http://a/b/c/d;p?q#")
        self.checkJoin(RFC1808_BASE, "#z", "http://a/b/c/d;p?q#z")
        # self.checkJoin(RFC1808_BASE, "?", "http://a/b/c/d;p?")
        # self.checkJoin(RFC1808_BASE, "?#z", "http://a/b/c/d;p?#z")
        self.checkJoin(RFC1808_BASE, "?y", "http://a/b/c/d;p?y")
        # self.checkJoin(RFC1808_BASE, ";", "http://a/b/c/;")
        # self.checkJoin(RFC1808_BASE, ";?y", "http://a/b/c/;?y")
        # self.checkJoin(RFC1808_BASE, ";#z", "http://a/b/c/;#z")
        self.checkJoin(RFC1808_BASE, ";x", "http://a/b/c/;x")
        self.checkJoin(RFC1808_BASE, "/w", "http://a/w")
        # self.checkJoin(RFC1808_BASE, "//", "http://a/b/c/d;p?q#f")
        self.checkJoin(RFC1808_BASE, "//#z", "http://a/b/c/d;p?q#z")
        self.checkJoin(RFC1808_BASE, "//?y", "http://a/b/c/d;p?y")
        self.checkJoin(RFC1808_BASE, "//;x", "http://;x")
        self.checkJoin(RFC1808_BASE, "///w", "http://a/w")
        self.checkJoin(RFC1808_BASE, "//v", "http://v")
        # For backward compatibility with RFC1630, the scheme name is allowed
        # to be present in a relative reference if it is the same as the base
        # URI scheme.
        # self.checkJoin(RFC1808_BASE, "http:", "http://a/b/c/d;p?q#f")
        # self.checkJoin(RFC1808_BASE, "http:#", "http://a/b/c/d;p?q#")
        self.checkJoin(RFC1808_BASE, "http:#z", "http://a/b/c/d;p?q#z")
        # self.checkJoin(RFC1808_BASE, "http:?", "http://a/b/c/d;p?")
        # self.checkJoin(RFC1808_BASE, "http:?#z", "http://a/b/c/d;p?#z")
        self.checkJoin(RFC1808_BASE, "http:?y", "http://a/b/c/d;p?y")
        # self.checkJoin(RFC1808_BASE, "http:;", "http://a/b/c/;")
        # self.checkJoin(RFC1808_BASE, "http:;?y", "http://a/b/c/;?y")
        # self.checkJoin(RFC1808_BASE, "http:;#z", "http://a/b/c/;#z")
        self.checkJoin(RFC1808_BASE, "http:;x", "http://a/b/c/;x")
        self.checkJoin(RFC1808_BASE, "http:/w", "http://a/w")
        # self.checkJoin(RFC1808_BASE, "http://", "http://a/b/c/d;p?q#f")
        self.checkJoin(RFC1808_BASE, "http://#z", "http://a/b/c/d;p?q#z")
        self.checkJoin(RFC1808_BASE, "http://?y", "http://a/b/c/d;p?y")
        self.checkJoin(RFC1808_BASE, "http://;x", "http://;x")
        self.checkJoin(RFC1808_BASE, "http:///w", "http://a/w")
        self.checkJoin(RFC1808_BASE, "http://v", "http://v")
        # Different scheme is not ignored.

        # !!2 different from urljoin because scheme http and https is different, and in this case urljoin return unaltered and unparsed relurl
        # !!2 self.checkJoin(RFC1808_BASE, "https:", "https:")
        # !!2 self.checkJoin(RFC1808_BASE, "https:#", "https:#")
        # !!2 self.checkJoin(RFC1808_BASE, "https:#z", "https:#z")
        # !!2 self.checkJoin(RFC1808_BASE, "https:?", "https:?")
        # !!2 self.checkJoin(RFC1808_BASE, "https:?y", "https:?y")
        # !!2 self.checkJoin(RFC1808_BASE, "https:;", "https:;")
        # !!2 self.checkJoin(RFC1808_BASE, "https:;x", "https:;x")

        # !!1 were commented out because urljoin doesn't parse urls if either base or relurl are empty, returns the same strings

    def test_urljoins_relative_base(self):
        # According to RFC 3986, Section 5.1, a base URI must conform to
        # the absolute-URI syntax rule (Section 4.3). But urljoin() lacks
        # a context to establish missed components of the relative base URI.
        # It still has to return a sensible result for backwards compatibility.
        # The following tests are figments of the imagination and artifacts
        # of the current implementation that are not based on any standard.
        self.checkJoin("", "", "")
        # !!1 self.checkJoin("", "//", "//")
        self.checkJoin("", "//v", "//v")
        self.checkJoin("", "//v/w", "//v/w")
        self.checkJoin("", "/w", "/w")
        # !!1 self.checkJoin("", "///w", "///w")
        self.checkJoin("", "w", "w")

        # !!1 self.checkJoin("//", "", "//")
        # self.checkJoin("//", "//", "//")
        self.checkJoin("//", "//v", "//v")
        self.checkJoin("//", "//v/w", "//v/w")
        # self.checkJoin("//", "/w", "///w")
        # self.checkJoin("//", "///w", "///w")
        # self.checkJoin("//", "w", "///w")

        self.checkJoin("//a", "", "//a")
        self.checkJoin("//a", "//", "//a")
        self.checkJoin("//a", "//v", "//v")
        self.checkJoin("//a", "//v/w", "//v/w")
        self.checkJoin("//a", "/w", "//a/w")
        self.checkJoin("//a", "///w", "//a/w")
        self.checkJoin("//a", "w", "//a/w")

        for scheme in "", "http:":
            # self.checkJoin("http:", scheme + "", "http:")
            # self.checkJoin("http:", scheme + "//", "http:")
            self.checkJoin("http:", scheme + "//v", "http://v")
            self.checkJoin("http:", scheme + "//v/w", "http://v/w")
            # self.checkJoin("http:", scheme + "/w", "http:/w")
            # self.checkJoin("http:", scheme + "///w", "http:/w")
            # self.checkJoin("http:", scheme + "w", "http:/w")

            self.checkJoin("http://", scheme + "w", "http:///w")
            self.checkJoin("http://", scheme + "", "http://")
            self.checkJoin("http://", scheme + "//", "http://")
            self.checkJoin("http://", scheme + "//v", "http://v")
            self.checkJoin("http://", scheme + "//v/w", "http://v/w")
            self.checkJoin("http://", scheme + "/w", "http:///w")
            self.checkJoin("http://", scheme + "///w", "http:///w")

            self.checkJoin("http://a", scheme + "", "http://a")
            self.checkJoin("http://a", scheme + "//", "http://a")
            self.checkJoin("http://a", scheme + "//v", "http://v")
            self.checkJoin("http://a", scheme + "//v/w", "http://v/w")
            self.checkJoin("http://a", scheme + "/w", "http://a/w")
            self.checkJoin("http://a", scheme + "///w", "http://a/w")
            self.checkJoin("http://a", scheme + "w", "http://a/w")

        self.checkJoin("/b/c", "", "/b/c")
        self.checkJoin("/b/c", "//", "/b/c")
        self.checkJoin("/b/c", "//v", "//v")
        self.checkJoin("/b/c", "//v/w", "//v/w")
        self.checkJoin("/b/c", "/w", "/w")
        self.checkJoin("/b/c", "///w", "/w")
        self.checkJoin("/b/c", "w", "/b/w")

        # !!1 self.checkJoin("///b/c", "", "///b/c")
        # self.checkJoin("///b/c", "//", "///b/c")
        self.checkJoin("///b/c", "//v", "//v")
        self.checkJoin("///b/c", "//v/w", "//v/w")
        # self.checkJoin("///b/c", "/w", "///w")
        # self.checkJoin("///b/c", "///w", "///w")
        # self.checkJoin("///b/c", "w", "///b/w")


UrlParseTestCase().test()
