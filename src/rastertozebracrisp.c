// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnny Lee

#include <cups/cups.h>
#include <cups/ppd.h>
#include <cups/raster.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define RASTERTOLABEL "/usr/libexec/cups/filter/rastertolabel"
#define MAX_DIMENSION 65535u
#define MAX_IN_BYTES  (MAX_DIMENSION * 2)
#define MAX_OUT_BYTES ((MAX_DIMENSION + 7) / 8)

// macOS color-corrects gray before the filter gets it. These K values are measured.
enum {
  THRESHOLD_BOLD   = 41,
  THRESHOLD_NORMAL = 79,
  THRESHOLD_THIN   = 147,
};

static uint8_t in_row[MAX_IN_BYTES];
static uint8_t out_row[MAX_OUT_BYTES];

static bool header_is_valid(const cups_page_header2_t *header)
{
  unsigned bpp = header->cupsBitsPerPixel;

  if (bpp != 1 && bpp != 8 && bpp != 16)
    return false;
  if (header->cupsColorSpace != CUPS_CSPACE_K && header->cupsColorSpace != CUPS_CSPACE_W &&
      header->cupsColorSpace != CUPS_CSPACE_SW)
    return false;
  if (header->cupsWidth == 0 || header->cupsWidth > MAX_DIMENSION ||
      header->cupsHeight == 0 || header->cupsHeight > MAX_DIMENSION)
    return false;
  return header->cupsBytesPerLine == ((uint64_t)header->cupsWidth * bpp + 7) / 8;
}

static unsigned out_bytes_per_line(const cups_page_header2_t *header)
{
  return (header->cupsWidth + 7) / 8;
}

static int black_level(const cups_page_header2_t *header, unsigned x)
{
  int k;

  switch (header->cupsBitsPerPixel) {
  case 1:
    k = (in_row[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0;
    break;
  case 8:
    k = in_row[x];
    break;
  default: {
    uint16_t value;
    memcpy(&value, in_row + 2 * x, sizeof(value));
    k = value >> 8;
  }
  }
  return header->cupsColorSpace == CUPS_CSPACE_K ? k : 255 - k;
}

static bool read_row(cups_raster_t *in, const cups_page_header2_t *header, int threshold)
{
  memset(out_row, 0, out_bytes_per_line(header));
  if (cupsRasterReadPixels(in, in_row, header->cupsBytesPerLine) < header->cupsBytesPerLine)
    return false;
  for (unsigned x = 0; x < header->cupsWidth; x++)
    if (black_level(header, x) >= threshold)
      out_row[x >> 3] |= 0x80 >> (x & 7);
  return true;
}

typedef enum {
  PAGE_END,
  PAGE_OK,
  PAGE_INVALID,
} page_status_t;

static page_status_t read_page_header(cups_raster_t *in, cups_page_header2_t *header)
{
  if (!cupsRasterReadHeader2(in, header))
    return PAGE_END;
  if (header_is_valid(header))
    return PAGE_OK;
  fputs("ERROR: Unsupported or damaged page data\n", stderr);
  return PAGE_INVALID;
}

static int line_weight_threshold(const char *job_options)
{
  const char *path = getenv("PPD");
  ppd_file_t *ppd = path ? ppdOpenFile(path) : NULL;
  int threshold = THRESHOLD_NORMAL;

  if (!ppd)
    return threshold;

  cups_option_t *options = NULL;
  int num_options = cupsParseOptions(job_options, 0, &options);
  ppdMarkDefaults(ppd);
  cupsMarkOptions(ppd, num_options, options);

  ppd_choice_t *weight = ppdFindMarkedChoice(ppd, "zeCrispLineWeight");
  if (weight && !strcmp(weight->choice, "Thin"))
    threshold = THRESHOLD_THIN;
  else if (weight && !strcmp(weight->choice, "Bold"))
    threshold = THRESHOLD_BOLD;

  cupsFreeOptions(num_options, options);
  ppdClose(ppd);
  return threshold;
}

typedef struct {
  long    offset;
  long   *xref;
  size_t  count;
} pdf_t;

static void pdf_write(pdf_t *pdf, const void *data, size_t length)
{
  pdf->offset += fwrite(data, 1, length, stdout);
}

static void pdf_printf(pdf_t *pdf, const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  int length = vprintf(format, ap);
  va_end(ap);
  if (length > 0)
    pdf->offset += length;
}

static bool pdf_reserve(pdf_t *pdf, size_t count)
{
  if (count <= pdf->count)
    return true;

  long *xref = realloc(pdf->xref, count * sizeof(*xref));
  if (!xref) {
    fputs("ERROR: Out of memory\n", stderr);
    return false;
  }
  memset(xref + pdf->count, 0, (count - pdf->count) * sizeof(*xref));
  pdf->xref  = xref;
  pdf->count = count;
  return true;
}

static void pdf_object(pdf_t *pdf, size_t number)
{
  pdf->xref[number] = pdf->offset;
  pdf_printf(pdf, "%zu 0 obj\n", number);
}

static double points(unsigned pixels, unsigned dpi)
{
  return pixels * 72.0 / (dpi ? dpi : 203);
}

static int write_preview(cups_raster_t *in, int threshold)
{
  pdf_t pdf = { 0 };
  cups_page_header2_t header;
  page_status_t page_status;
  size_t pages = 0;
  int status = 1;

  if (!pdf_reserve(&pdf, 3))
    goto done;
  pdf_printf(&pdf, "%%PDF-1.4\n");

  while ((page_status = read_page_header(in, &header)) == PAGE_OK) {
    size_t page = 3 + 3 * pages++;
    if (!pdf_reserve(&pdf, page + 3))
      goto done;

    double width = points(header.cupsWidth, header.HWResolution[0]);
    double height = points(header.cupsHeight, header.HWResolution[1]);
    unsigned bpl = out_bytes_per_line(&header);
    char contents[96];
    int contents_length = snprintf(contents, sizeof(contents), "q %.3f 0 0 %.3f 0 0 cm /Im0 Do Q\n",
                                   width, height);

    pdf_object(&pdf, page);
    pdf_printf(&pdf, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.3f %.3f] "
                     "/Resources << /XObject << /Im0 %zu 0 R >> >> /Contents %zu 0 R >>\nendobj\n",
               width, height, page + 2, page + 1);

    pdf_object(&pdf, page + 1);
    pdf_printf(&pdf, "<< /Length %d >>\nstream\n%sendstream\nendobj\n", contents_length, contents);

    pdf_object(&pdf, page + 2);
    pdf_printf(&pdf, "<< /Type /XObject /Subtype /Image /Width %u /Height %u /ColorSpace /DeviceGray "
                     "/BitsPerComponent 1 /Decode [1 0] /Interpolate false /Length %llu >>\nstream\n",
               header.cupsWidth, header.cupsHeight, (unsigned long long)bpl * header.cupsHeight);
    bool complete = true;
    for (unsigned y = 0; y < header.cupsHeight; y++) {
      if (complete)
        complete = read_row(in, &header, threshold);
      else
        memset(out_row, 0, bpl);
      pdf_write(&pdf, out_row, bpl);
    }
    pdf_printf(&pdf, "\nendstream\nendobj\n");
  }

  if (page_status == PAGE_INVALID)
    goto done;
  if (!pages) {
    fputs("ERROR: No pages found\n", stderr);
    goto done;
  }

  pdf_object(&pdf, 1);
  pdf_printf(&pdf, "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
  pdf_object(&pdf, 2);
  pdf_printf(&pdf, "<< /Type /Pages /Count %zu /Kids [", pages);
  for (size_t i = 0; i < pages; i++)
    pdf_printf(&pdf, " %zu 0 R", 3 + 3 * i);
  pdf_printf(&pdf, " ] >>\nendobj\n");

  long startxref = pdf.offset;
  pdf_printf(&pdf, "xref\n0 %zu\n0000000000 65535 f \n", pdf.count);
  for (size_t i = 1; i < pdf.count; i++)
    pdf_printf(&pdf, "%010ld 00000 n \n", pdf.xref[i]);
  pdf_printf(&pdf, "trailer\n<< /Size %zu /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", pdf.count, startxref);
  status = fflush(stdout) || ferror(stdout) ? 1 : 0;

done:
  free(pdf.xref);
  return status;
}

static int print_label(cups_raster_t *in, int threshold, char *argv[])
{
  int pipefd[2];
  if (pipe(pipefd)) {
    perror("ERROR: pipe");
    return 1;
  }

  pid_t child = fork();
  if (child < 0) {
    perror("ERROR: fork");
    return 1;
  }
  if (child == 0) {
    dup2(pipefd[0], STDIN_FILENO);
    close(pipefd[0]);
    close(pipefd[1]);
    execl(RASTERTOLABEL, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], (char *)NULL);
    fprintf(stderr, "ERROR: Unable to run %s: %s\n", RASTERTOLABEL, strerror(errno));
    _exit(1);
  }
  close(pipefd[0]);

  cups_raster_t *out = cupsRasterOpen(pipefd[1], CUPS_RASTER_WRITE);
  cups_page_header2_t header;
  page_status_t page_status;
  int pages = 0;

  while ((page_status = read_page_header(in, &header)) == PAGE_OK) {
    fprintf(stderr, "INFO: Converting page %d\n", ++pages);

    cups_page_header2_t out_header = header;
    out_header.cupsBitsPerColor = 1;
    out_header.cupsBitsPerPixel = 1;
    out_header.cupsBytesPerLine = out_bytes_per_line(&header);
    out_header.cupsColorSpace   = CUPS_CSPACE_K;
    cupsRasterWriteHeader2(out, &out_header);

    bool complete = true;
    for (unsigned y = 0; y < header.cupsHeight; y++) {
      if (complete)
        complete = read_row(in, &header, threshold);
      else
        memset(out_row, 0, out_header.cupsBytesPerLine);
      cupsRasterWritePixels(out, out_row, out_header.cupsBytesPerLine);
    }
  }

  cupsRasterClose(out);
  close(pipefd[1]);

  int status;
  pid_t waited;
  do
    waited = waitpid(child, &status, 0);
  while (waited < 0 && errno == EINTR);

  if (page_status == PAGE_END && !pages)
    fputs("ERROR: No pages found\n", stderr);
  return page_status == PAGE_END && pages && waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
}

int main(int argc, char *argv[])
{
  if (argc < 6 || argc > 7) {
    fprintf(stderr, "Usage: %s job user title copies options [file]\n", argv[0]);
    return 1;
  }

  int in_fd = STDIN_FILENO;
  if (argc == 7 && (in_fd = open(argv[6], O_RDONLY)) < 0) {
    fprintf(stderr, "ERROR: Unable to open %s: %s\n", argv[6], strerror(errno));
    return 1;
  }

  int threshold = line_weight_threshold(argv[5]);
  cups_raster_t *in = cupsRasterOpen(in_fd, CUPS_RASTER_READ);
  int status = getenv("ZEBRA_CRISP_PREVIEW") ? write_preview(in, threshold)
                                             : print_label(in, threshold, argv);
  cupsRasterClose(in);
  return status;
}
