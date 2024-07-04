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
#include "utils/maps.hpp"
#include <algorithm>  // std::find_if
#include <cinttypes>  // macros PRIu64 and PRId64
#include <cstdint>  // int64_t, uint64_t
#include <cstdio>  // std::FILE, std::fprintf, std::fclose
#include <iterator>  // std::distance
#include <vector>


constexpr unsigned int n_characters = 256;

struct statistics {
  std::vector<uint64_t> sequence_chars;
  std::vector<uint64_t> quality_chars;
  std::vector<uint64_t> tail_chars;
  std::vector<int> maxrun;
  uint64_t total_chars = 0;
  uint64_t seq_count = 0;
  int qmin_n = 255;
  int qmax_n = 0;
  char qmin = '\0';
  char qmax = '\0';
  char fastq_ascii = '\0';
  char fastq_qmin = '\0';
  char fastq_qmax = '\0';
};


namespace {

  auto guess_quality_offset(struct statistics & stats) -> void {
    static constexpr auto lowerbound = ';';  // char 59 (-5 to offset +64)
    static constexpr auto upperbound = 'K';  // char 75 (+1 after offset +33 normal range)

    if ((stats.qmin < lowerbound) or (stats.qmax < upperbound)) {
      stats.fastq_ascii = static_cast<char>(default_ascii_offset);  // +33, from vsearch.h
    }
    else {
      stats.fastq_ascii = static_cast<char>(alternative_ascii_offset);  // +64, from vsearch.h
    }
    stats.fastq_qmax = stats.qmax - stats.fastq_ascii;
    stats.fastq_qmin = stats.qmin - stats.fastq_ascii;
  }


  auto find_lowest_quality_symbol(struct statistics & stats) -> void {
    auto lowest = std::find_if(stats.quality_chars.cbegin(),
                               stats.quality_chars.cend(),
                               [](uint64_t const counter) -> bool {
                                 return counter != 0;
                               });
    if (lowest != stats.quality_chars.cend()) {
      stats.qmin = std::distance(stats.quality_chars.cbegin(), lowest);
    }
  }


  auto find_highest_quality_symbol(struct statistics & stats) -> void {
    // note: searching using reverse iterators
    auto highest = std::find_if(stats.quality_chars.rbegin(),
                                stats.quality_chars.rend(),
                                [](uint64_t const counter) -> bool {
                                  return counter != 0; }
                                );
    if (highest != stats.quality_chars.rend()) {
      stats.qmax = std::distance(highest, stats.quality_chars.rend()) - 1;
    }
  }


  auto stats_message(std::FILE * output_stream,
                     struct statistics const & stats) -> void {
    std::fprintf(output_stream, "Read %" PRIu64 " sequences.\n", stats.seq_count);

    if (stats.seq_count > 0)
      {
        std::fprintf(output_stream, "Qmin %d, Qmax %d, Range %d\n",
                stats.qmin, stats.qmax, stats.qmax - stats.qmin + 1);

        std::fprintf(output_stream, "Guess: -fastq_qmin %d -fastq_qmax %d -fastq_ascii %d\n",
                stats.fastq_qmin, stats.fastq_qmax, stats.fastq_ascii);

        if (stats.fastq_ascii == 64)
          {
            if (stats.qmin < 64)
              {
                std::fprintf(output_stream, "Guess: Solexa format (phred+64)\n");
              }
            else if (stats.qmin < 66)
              {
                std::fprintf(output_stream, "Guess: Illumina 1.3+ format (phred+64)\n");
              }
            else
              {
                std::fprintf(output_stream, "Guess: Illumina 1.5+ format (phred+64)\n");
              }
          }
        else
          {
            if (stats.qmax > 73)
              {
                std::fprintf(output_stream, "Guess: Illumina 1.8+ format (phred+33)\n");
              }
            else
              {
                std::fprintf(output_stream, "Guess: Original Sanger format (phred+33)\n");
              }
          }

        std::fprintf(output_stream, "\n");
        std::fprintf(output_stream, "Letter          N   Freq MaxRun\n");
        std::fprintf(output_stream, "------ ---------- ------ ------\n");

        for (auto c = 0; c < 256; ++c)
          {
            if (stats.sequence_chars[c] == 0) { continue; }
            std::fprintf(output_stream, "     %c %10" PRIu64 " %5.1f%% %6d",
                    c,
                    stats.sequence_chars[c],
                    100.0 * stats.sequence_chars[c] / stats.total_chars,
                    stats.maxrun[c]);
            if ((c == 'N') or (c == 'n'))
              {
                if (stats.qmin_n < stats.qmax_n)
                  {
                    std::fprintf(output_stream, "  Q=%c..%c", stats.qmin_n, stats.qmax_n);
                  }
                else
                  {
                    std::fprintf(output_stream, "  Q=%c", stats.qmin_n);
                  }
              }
            std::fprintf(output_stream, "\n");
          }

        std::fprintf(output_stream, "\n");
        std::fprintf(output_stream, "Char  ASCII    Freq       Tails\n");
        std::fprintf(output_stream, "----  -----  ------  ----------\n");

        for (int c = stats.qmin; c <= stats.qmax; ++c)
          {
            if (stats.quality_chars[c] > 0)
              {
                std::fprintf(output_stream, " '%c'  %5d  %5.1f%%  %10" PRIu64 "\n",
                        c,
                        c,
                        100.0 * stats.quality_chars[c] / stats.total_chars,
                        stats.tail_chars[c]);
              }
          }
      }
  }


  auto output_stats_message(struct Parameters const & parameters,
                            struct statistics const & stats,
                            char const * log_filename) -> void {
    if (log_filename == nullptr) {
      return;
    }
    stats_message(parameters.fp_log, stats);
  }


  auto output_stats_message(struct Parameters const & parameters,
                            struct statistics const & stats) -> void {
    if (parameters.opt_quiet) {
      return;
    }
    stats_message(stderr, stats);
  }
}


auto fastq_chars(struct Parameters const & parameters) -> void
{
  struct statistics stats;
  stats.sequence_chars.resize(n_characters);
  stats.quality_chars.resize(n_characters);
  stats.tail_chars.resize(n_characters);
  stats.maxrun.resize(n_characters);

  auto fastq_handle = fastq_open(parameters.opt_fastq_chars);

  auto const filesize = fastq_get_size(fastq_handle);

  progress_init("Reading FASTQ file", filesize);

  while (fastq_next(fastq_handle, false, chrmap_upcase_vector.data()))
    {
      int64_t const len = fastq_get_sequence_length(fastq_handle);
      auto * seq_ptr = fastq_get_sequence(fastq_handle);
      auto * qual_ptr = fastq_get_quality(fastq_handle);

      ++stats.seq_count;
      stats.total_chars += len;

      auto run_char = -1;
      auto run = 0;

      int64_t i = 0;
      while (i < len)
        {
          int const pc = *seq_ptr;
          ++seq_ptr;
          int const qc = *qual_ptr;
          ++qual_ptr;
          ++stats.sequence_chars[pc];
          ++stats.quality_chars[qc];

          if ((pc == 'N') or (pc == 'n'))
            {
              if (qc < stats.qmin_n)
                {
                  stats.qmin_n = qc;
                }
              if (qc > stats.qmax_n)
                {
                  stats.qmax_n = qc;
                }
            }

          if (pc == run_char)
            {
              ++run;
              if (run > stats.maxrun[run_char])
                {
                  stats.maxrun[run_char] = run;
                }
            }
          else
            {
              run_char = pc;
              run = 0;
            }

          ++i;
        }

      if (len >= parameters.opt_fastq_tail)
        {
          qual_ptr = fastq_get_quality(fastq_handle) + len - 1;
          int const tail_char = *qual_ptr;
          --qual_ptr;
          auto tail_len = 1;
          while (*qual_ptr == tail_char)
            {
              --qual_ptr;
              ++tail_len;
              if (tail_len >= parameters.opt_fastq_tail)
                {
                  break;
                }
            }
          if (tail_len >= parameters.opt_fastq_tail)
            {
              ++stats.tail_chars[tail_char];
            }
        }

      progress_update(fastq_get_position(fastq_handle));
    }
  progress_done();

  fastq_close(fastq_handle);

  find_lowest_quality_symbol(stats);
  find_highest_quality_symbol(stats);
  guess_quality_offset(stats);

  output_stats_message(parameters, stats);
  output_stats_message(parameters, stats, parameters.opt_log);
}
