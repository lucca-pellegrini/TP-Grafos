#!/usr/bin/env perl

# SPDX-License-Identifier: ISC
# SPDX-FileCopyrightText: Copyright © 2026 Lucca M. A. Pellegrini <lucca@verticordia.com>

# Script auxiliar para ler os datasets do OpenFlights e gerar:
#   1. (stdout) Cabeçalho C com o grafo como constantes estáticas
#   2. (--csr-out) Diretório com formato CSR binário para Python/C dinâmico
# Lê diretório como último argumento.

use strict;
use warnings;

use File::Path qw(make_path);

my $csr_out = undef;
my $data_dir;

while (@ARGV) {
    my $arg = shift;
    if ($arg eq '--csr-out' && @ARGV) {
        $csr_out = shift;
    } else {
        $data_dir = $arg;
        last;
    }
}

$data_dir //= shift @ARGV;

die "Usage: $0 [--csr-out <dir>] <data_dir>\n" unless defined $data_dir;

my $airports_file = "$data_dir/airports.dat";
my $routes_file   = "$data_dir/routes.dat";

-d $data_dir      or die "Not a directory: $data_dir\n";
-f $airports_file or die "Not found: $airports_file\n";
-f $routes_file   or die "Not found: $routes_file\n";

binmode(STDOUT, ':utf8');

# Parser de CSV ingênuo. Consegue lidar com termos entre aspas (mais ou menos).
sub parse_csv {
    my $line = shift;
    my @fields;
    while ($line =~ /\G (?:"([^"]*)" | ([^,]*)) /gcx) {
        push @fields, defined $1 ? $1 : ($2 // '');
        last unless $line =~ /\G , /gcx;
    }
    return @fields;
}

# Converte uma string para usar em código fonte C, usando ASCII e escapando.
sub clean_c_string {
    my ($s, $maxlen) = @_;

    # Translitera alguns acentos comuns.
    my %tr = (
        "\x{00C0}" => 'A', "\x{00C1}" => 'A', "\x{00C2}" => 'A', "\x{00C3}" => 'A',
        "\x{00C8}" => 'E', "\x{00C9}" => 'E', "\x{00CA}" => 'E', "\x{00CB}" => 'E',
        "\x{00CC}" => 'I', "\x{00CD}" => 'I', "\x{00CE}" => 'I', "\x{00CF}" => 'I',
        "\x{00D2}" => 'O', "\x{00D3}" => 'O', "\x{00D4}" => 'O', "\x{00D5}" => 'O',
        "\x{00D9}" => 'U', "\x{00DA}" => 'U', "\x{00DB}" => 'U',
        "\x{00C7}" => 'C',
        "\x{00D1}" => 'N',
        "\x{00E0}" => 'a', "\x{00E1}" => 'a', "\x{00E2}" => 'a', "\x{00E3}" => 'a',
        "\x{00E8}" => 'e', "\x{00E9}" => 'e', "\x{00EA}" => 'e', "\x{00EB}" => 'e',
        "\x{00EC}" => 'i', "\x{00ED}" => 'i', "\x{00EE}" => 'i', "\x{00EF}" => 'i',
        "\x{00F2}" => 'o', "\x{00F3}" => 'o', "\x{00F4}" => 'o', "\x{00F5}" => 'o',
        "\x{00F9}" => 'u', "\x{00FA}" => 'u', "\x{00FB}" => 'u',
        "\x{00E7}" => 'c',
        "\x{00F1}" => 'n',
        "\x{00DF}" => 'ss',
        "\x{00D6}" => 'O', "\x{00F6}" => 'o',
        "\x{00DC}" => 'U', "\x{00FC}" => 'u',
        "\x{2013}" => '-', "\x{2014}" => '-',
    );
    $s =~ s/([\x{80}-\x{10FFFF}])/exists $tr{$1} ? $tr{$1} : '.'/eg;
    $s =~ s/[[:cntrl:]]/./g;

    # Trunca para caber no array de tamanho estático.
    $s = substr($s, 0, $maxlen - 1);
    $s =~ s/\\/\\\\/g;
    $s =~ s/"/\\"/g;
    return $s;
}

## Lê os aeroportos

my %airport;
my $airport_count = 0;

open my $afh, '<:encoding(UTF-8)', $airports_file or warn "Cannot open $airports_file: $!\n" and exit 1;
while (my $line = <$afh>) {
    chomp $line;
    next if $line =~ /^\s*$/;
    my @f = parse_csv($line);
    next if @f < 8;
    my $id = $f[0];
    $airport{$id} = {
        name    => $f[1] // '',
        city    => $f[2] // '',
        country => $f[3] // '',
        iata    => $f[4] // '',
    };
    $airport_count++;
}
close $afh;

## Lê as rotas

my %route_airport;
my @raw_edges;

open my $rfh, '<:crlf', $routes_file or die "Cannot open $routes_file: $!\n";
while (my $line = <$rfh>) {
    chomp $line;
    next if $line =~ /^\s*$/;
    my @f = parse_csv($line);
    next if @f < 5;

    my $src_id = $f[3];
    my $dst_id = $f[5];
    next if $src_id eq '\N' or $dst_id eq '\N';
    next if $src_id eq '' or $dst_id eq '';
    next if $src_id eq $dst_id;

    $route_airport{$src_id} = 1;
    $route_airport{$dst_id} = 1;
    push @raw_edges, [$src_id, $dst_id];
}
close $rfh;

## Filtra os aeroportos

my @node_ids = sort { $a <=> $b } grep { exists $airport{$_} } keys %route_airport;
my %id_to_idx;
for my $i (0 .. $#node_ids) {
    $id_to_idx{ $node_ids[$i] } = $i;
}

my $n = scalar @node_ids;

## Constrói todas as arestas

my @edges;
for my $e (@raw_edges) {
    my ($src, $dst) = @$e;
    next unless exists $id_to_idx{$src} and exists $id_to_idx{$dst};
    push @edges, [$id_to_idx{$src}, $id_to_idx{$dst}];
}

my $m = scalar @edges;

## Gera dois arrays para representar o grafo em CSR.
## O índice de um vértice no array `offsets` aponta para o índice em `edges`
## onde começam-se os seus vizinhos.
## Em teoria é muito bom, já que o grafo é esparso e a matriz teria
## complexidade de uso de memória de ordem O(V²).

# Ordena arestas tal que cada elemento em `edges` seja uma referência para um
# array `[u, v]` onde `u` é a origem e `v` é o destino. Necessário para CSR!
@edges = sort { $a->[0] <=> $b->[0] || $a->[1] <=> $b->[1] } @edges;

# Inicializa o array de tamanho `n + 1`.
my @offsets = (0) x ($n + 1);

# Executa a primeira passagem, que incrementa a posição u+1 para cada aresta.
# Ao final, `offsets` contém o número de vizinhos (grau) de cada vértice `i-1`.
for my $e (@edges) {
    $offsets[ $e->[0] + 1 ]++;
}

# Segunda passagem calcula os offsets reais (soma cada grau ao offset anterior).
for my $i (1 .. $n) {
    $offsets[$i] += $offsets[$i - 1];
}

# Confere se o computador não está lelé da cuca.
my $check_m = $offsets[$n];
$check_m == $m or die "CSR mismatch: offsets[$n]=$check_m vs edges=$m\n";

## Exibe algumas estatísticas pra conferirmos visualmente se tá tudo certo.

my $max_deg = 0;
my $sum_deg = 0;
for my $i (0 .. $n - 1) {
    my $d = $offsets[$i + 1] - $offsets[$i];
    $max_deg = $d if $d > $max_deg;
    $sum_deg += $d;
}

warn "airports.dat: $airport_count airports\n";
warn "routes.dat:   " . (scalar @raw_edges) . " routes\n";
warn "graph:        $n nodes, $m edges\n";
warn "avg degree:   " . sprintf("%.2f", $sum_deg / $n) . "\n";
warn "max degree:   $max_deg\n";


## Serializa o grafo em formato CSR binário (para Python / C dinâmico)

if (defined $csr_out) {
    make_path($csr_out) unless -d $csr_out;

    # Metadados: n, m, nome, direcionado
    open my $mfh, '>:utf8', "$csr_out/graph.meta"
        or die "Cannot write $csr_out/graph.meta: $!\n";
    print $mfh "$n\n$m\nopenflights\n1\n"; # 1 = directed
    close $mfh;

    # Offsets: (n+1) × int32 little-endian
    open my $ofh, '>:raw', "$csr_out/offsets.bin"
        or die "Cannot write $csr_out/offsets.bin: $!\n";
    for my $i (0 .. $n) {
        print $ofh pack('V', $offsets[$i]);
    }
    close $ofh;

    # Edges: m × int32 little-endian
    open my $efh, '>:raw', "$csr_out/edges.bin"
        or die "Cannot write $csr_out/edges.bin: $!\n";
    for my $e (@edges) {
        print $efh pack('V', $e->[1]); # destination only, same as C CSR
    }
    close $efh;

    # Metadados dos nós: iata,name,city,country (CSV)
    open my $nvfh, '>:utf8', "$csr_out/nodes.csv"
        or die "Cannot write $csr_out/nodes.csv: $!\n";
    print $nvfh "iata,name,city,country\n";
    for my $i (0 .. $n - 1) {
        my $id = $node_ids[$i];
        my $iata    = $airport{$id}{iata};
        my $name    = $airport{$id}{name};
        my $city    = $airport{$id}{city};
        my $country = $airport{$id}{country};
        # Quote fields that may contain commas
        $name    = qq("$name")    if $name    =~ /,/;
        $city    = qq("$city")    if $city    =~ /,/;
        $country = qq("$country") if $country =~ /,/;
        printf $nvfh "%s,%s,%s,%s\n", $iata, $name, $city, $country;
    }
    close $nvfh;

    warn "CSR binary written to $csr_out/\n";
}

## Gera o cabeçalho fonte em C, rezando para estar válido.

print <<"EOF";
/*
 * SPDX-License-Identifier: ISC
 * SPDX-FileCopyrightText: Copyright \x{00A9} 2026 Lucca M. A. Pellegrini <lucca\@verticordia.com>
 *
 * graph_data.h \x{2014} Auto-generated by scripts/preprocess.pl
 * DO NOT EDIT. Generated from OpenFlights datasets.
 */

#ifndef GRAPH_DATA_H
#define GRAPH_DATA_H

#define NUM_NODES $n
#define NUM_EDGES $m

EOF

sub print_string_array {
    my ($name, $width, $nodes, $field) = @_;
    print "static const char $name" . "[NUM_NODES][$width] = {\n";
    for my $i (0 .. $n - 1) {
        my $id = $node_ids[$i];
        my $val = clean_c_string($airport{$id}{$field} // '', $width);
        printf "\t\"%s\",\n", $val;
    }
    print "};\n\n";
}

print_string_array('airport_iata',    4,  \@node_ids, 'iata');
print_string_array('airport_name',    64, \@node_ids, 'name');
print_string_array('airport_city',    64, \@node_ids, 'city');
print_string_array('airport_country', 64, \@node_ids, 'country');

# Offsets da representação CSR
print "static const int node_offsets[NUM_NODES + 1] = {\n\t";
for my $i (0 .. $n) {
    printf "%d", $offsets[$i];
    if ($i < $n) {
        print ",";
        print "\n\t" if (($i + 1) % 16 == 0);
        print " "  if (($i + 1) % 16 != 0);
    }
}
print "\n};\n\n";

# Arestas na representação CSR
print "static const int edges[NUM_EDGES] = {\n\t";
for my $i (0 .. $m - 1) {
    printf "%d", $edges[$i][1];
    if ($i < $m - 1) {
        print ",";
        print "\n\t" if (($i + 1) % 16 == 0);
        print " "  if (($i + 1) % 16 != 0);
    }
}
print "\n};\n\n";

print "#endif /* GRAPH_DATA_H */\n";
