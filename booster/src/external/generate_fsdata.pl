#!/usr/bin/perl

use strict;
use warnings;

use Cwd qw(abs_path getcwd);
use File::Basename qw(dirname);
use File::Copy qw(copy);
use File::Find;
use File::Path qw(make_path remove_tree);
use File::Spec;

sub read_file {
    my ($path) = @_;
    open(my $fh, '<:raw', $path) or die "Cannot open $path for reading: $!";
    local $/;
    my $content = <$fh>;
    close($fh);
    return defined $content ? $content : '';
}

sub write_file {
    my ($path, $content) = @_;
    open(my $fh, '>:raw', $path) or die "Cannot open $path for writing: $!";
    print {$fh} $content;
    close($fh);
}

sub minify_css {
    my ($css) = @_;

    $css =~ s/\r\n?/\n/g;
    $css =~ s{/\*.*?\*/}{}gs;
    $css =~ s/\s+/ /gs;
    $css =~ s/\s*([{}:;,>])\s*/$1/gs;
    $css =~ s/\s*\(\s*/(/gs;
    $css =~ s/\s*\)\s*/)/gs;
    $css =~ s/;}/}/g;
    $css =~ s/^\s+//;
    $css =~ s/\s+$//;

    return $css;
}

sub minify_script {
    my ($script) = @_;

    $script =~ s/\r\n?/\n/g;

    my @out;
    for my $line (split(/\n/, $script)) {
        $line =~ s/^\s+//;
        $line =~ s/\s+$//;
        next if $line eq '';
        next if $line =~ m{^//};
        push(@out, $line);
    }

    return join("\n", @out);
}

sub minify_markup_chunk {
    my ($chunk) = @_;

    $chunk =~ s/\r\n?/\n/g;
    $chunk =~ s/<!--(?!#).*?-->//gs;
    $chunk =~ s/\s+/ /gs;
    $chunk =~ s/>\s+</></gs;
    $chunk =~ s/\s*(<!--#.*?-->)\s*/$1/gs;
    $chunk =~ s/^\s+//;
    $chunk =~ s/\s+$//;

    return $chunk;
}

sub minify_html {
    my ($html) = @_;

    $html =~ s/\r\n?/\n/g;

    my $out = '';
    my $prev = 0;

    while ($html =~ m{(<(script|style|pre)\b[^>]*>)(.*?)(</\2>)}sig) {
        my $start = $-[0];
        my $end = $+[0];
        my $open_tag = $1;
        my $tag_name = lc($2);
        my $body = $3;
        my $close_tag = $4;

        $out .= minify_markup_chunk(substr($html, $prev, $start - $prev));
        if ($tag_name eq 'style') {
            $out .= $open_tag . minify_css($body) . $close_tag;
        } elsif ($tag_name eq 'pre') {
            $out .= $open_tag . $body . $close_tag;
        } else {
            $out .= $open_tag . minify_script($body) . $close_tag;
        }
        $prev = $end;
    }

    $out .= minify_markup_chunk(substr($html, $prev));
    return $out;
}

sub minify_text_file {
    my ($path, $content) = @_;

    if ($path =~ /\.(?:shtml|html|inc)\z/i) {
        return minify_html($content);
    }
    if ($path =~ /\.css\z/i) {
        return minify_css($content);
    }

    return $content;
}

my ($makefsdata_script, $source_dir, $output_file, $work_dir) = @ARGV;
if (!defined $work_dir) {
    die "Usage: $0 <makefsdata_script> <source_dir> <output_file> <work_dir>\n";
}

$makefsdata_script = abs_path($makefsdata_script)
  or die "Cannot resolve makefsdata script path\n";
$source_dir = abs_path($source_dir)
  or die "Cannot resolve source directory path\n";
$output_file = File::Spec->rel2abs($output_file);
$work_dir = File::Spec->rel2abs($work_dir);

remove_tree($work_dir) if -e $work_dir;
my $work_fs_dir = File::Spec->catdir($work_dir, 'fs');
make_path($work_fs_dir);

my @source_files;
find(
    {
        no_chdir => 1,
        wanted => sub {
            return if -d $File::Find::name;
            push(@source_files, $File::Find::name);
        },
    },
    $source_dir
);

@source_files = sort @source_files;

my $input_bytes = 0;
my $output_bytes = 0;
my $processed_files = 0;

for my $source_file (@source_files) {
    my $relative_path = File::Spec->abs2rel($source_file, $source_dir);
    my $dest_file = File::Spec->catfile($work_fs_dir, $relative_path);
    make_path(dirname($dest_file));

    my $input_size = -s $source_file;
    $input_bytes += $input_size;
    ++$processed_files;

    if ($source_file =~ /\.(?:shtml|html|inc|css)\z/i) {
        my $minified = minify_text_file($source_file, read_file($source_file));
        write_file($dest_file, $minified);
    } else {
        copy($source_file, $dest_file)
          or die "Cannot copy $source_file to $dest_file: $!";
    }

    $output_bytes += (-s $dest_file);
}

my $cwd = getcwd();
chdir($work_dir) or die "Cannot chdir to $work_dir: $!";
my $rc = system($^X, $makefsdata_script);
chdir($cwd) or die "Cannot chdir back to $cwd: $!";
if ($rc != 0) {
    die "makefsdata failed with exit code " . ($rc >> 8) . "\n";
}

my $generated_fsdata = File::Spec->catfile($work_dir, 'fsdata.c');
copy($generated_fsdata, $output_file)
  or die "Cannot copy $generated_fsdata to $output_file: $!";

my $saved_bytes = $input_bytes - $output_bytes;
my $saved_pct = $input_bytes > 0 ? (100.0 * $saved_bytes / $input_bytes) : 0.0;

printf(
    "Minified %d web assets: %d -> %d bytes (saved %d bytes, %.2f%%)\n",
    $processed_files,
    $input_bytes,
    $output_bytes,
    $saved_bytes,
    $saved_pct
);
