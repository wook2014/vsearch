/*

  VSEARCH: a versatile open source tool for metagenomics

  Copyright (C) 2014-2024, Torbjorn Rognes, Frederic Mahe and Tomas Flouri
  All rights reserved.

  Contact: Torbjorn Rognes <torognes@ifi.uio.no>,
  Department of Informatics, University of Oslo,
  PO Box 1080 Blindern, NO-0316 Oslo, Norway

  This software is dual-licensed and available under a choice
  of one of two licenses, either under the terms of the GNU
  General Public License version 3 or the BSD 2-Clause License.


  GNU General Public License version 3

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.


  The BSD 2-Clause License

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  1. Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
  COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

*/

#include "vsearch.h"
#include <cstdlib>
#include <cstdint>  // int64_t
#include <cstdio>  // std::FILE, std::fprintf
#include <cstring>  // std::strcmp
#include <vector>


struct sortinfo_size_s
{
  unsigned int size;
  unsigned int seqno;
};


auto sortbysize_compare(const void * lhs_a, const void * rhs_b) -> int
{
  auto * lhs = (struct sortinfo_size_s *) lhs_a;
  auto * rhs = (struct sortinfo_size_s *) rhs_b;

  /* highest abundance first, then by label, otherwise keep order */

  if (lhs->size < rhs->size)
    {
      return +1;
    }
  if (lhs->size > rhs->size)
    {
      return -1;
    }
  const int result = std::strcmp(db_getheader(lhs->seqno), db_getheader(rhs->seqno));
  if (result != 0)
    {
      return result;
    }
  if (lhs->seqno < rhs->seqno)
    {
      return -1;
    }
  if (lhs->seqno > rhs->seqno)
    {
      return +1;
    }
  return 0;
}


[[nodiscard]]
auto find_median_abundance(const int valid_amplicons,
                           std::vector<sortinfo_size_s> const & sortinfo_v) -> double
{
  // function returns a round value or a value with a remainder of 0.5
  static constexpr double half = 0.5;

  if (valid_amplicons == 0) {
    return 0.0;
  }

  // refactoring C++11: use const& std::vector.size()
  const auto midarray = std::div(valid_amplicons, 2);

  // odd number of valid amplicons
  if (valid_amplicons % 2 != 0)  {
    return sortinfo_v[midarray.quot].size * 1.0;  // a round value
  }

  // even number of valid amplicons
  // (average of two ints is either round or has a remainder of .5)
  // risk of silent overflow for large abundance values:
  // a >= b ; (a + b) / 2 == b + (a - b) / 2
  return sortinfo_v[midarray.quot].size +
    ((sortinfo_v[midarray.quot - 1].size - sortinfo_v[midarray.quot].size) * half);
}


auto sortbysize() -> void
{
  static constexpr auto one_hundred_percent = 100ULL;
  if (opt_output == nullptr) {
    fatal("FASTA output file for sortbysize must be specified with --output");
  }

  std::FILE * fp_output = fopen_output(opt_output);
  if (fp_output == nullptr)
    {
      fatal("Unable to open sortbysize output file for writing");
    }

  db_read(opt_sortbysize, 0);

  show_rusage();

  const auto dbsequencecount = db_getsequencecount();

  progress_init("Getting sizes", dbsequencecount);

  std::vector<struct sortinfo_size_s> sortinfo_v(dbsequencecount);

  int passed = 0;

  for(auto i = 0U; i < dbsequencecount; i++)
    {
      auto const size = static_cast<int64_t>(db_getabundance(i));

      if ((size < opt_minsize) or (size > opt_maxsize)) {
        continue;
      }

      sortinfo_v[passed].seqno = i;
      sortinfo_v[passed].size = static_cast<unsigned int>(size);
      ++passed;

      progress_update(i);
    }

  progress_done();

  show_rusage();

  auto * sortinfo = sortinfo_v.data();
  progress_init("Sorting", one_hundred_percent);
  qsort(sortinfo, passed, sizeof(sortinfo_size_s), sortbysize_compare);
  progress_done();

  const double median = find_median_abundance(passed, sortinfo_v);

  if (not opt_quiet)
    {
      static_cast<void>(fprintf(stderr, "Median abundance: %.0f\n", median));  // Banker's rounding (round half to even)
    }

  if (opt_log != nullptr)
    {
      static_cast<void>(fprintf(fp_log, "Median abundance: %.0f\n", median));  // Banker's rounding (round half to even)
    }

  show_rusage();

  passed = MIN(passed, opt_topn);

  progress_init("Writing output", passed);
  for(int i = 0; i < passed; i++)
    {
      fasta_print_db_relabel(fp_output, sortinfo_v[i].seqno, i + 1);
      progress_update(i);
    }
  progress_done();
  show_rusage();  // refactoring: why three calls to show_rusage()?

  db_free();
  if (fp_output != nullptr)
    {
      fclose(fp_output);
    }
}
