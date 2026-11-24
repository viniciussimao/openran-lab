#!/usr/bin/env bash
#
# slice-prb-share.sh: resume as linhas [SLICE] do log do gNB com o
# escalonador de slices (Etapa 2, fork do OAI) num trecho do teste.
#
# Soma todos os períodos (cerca de 1 s cada) do trecho e imprime, por slice,
# os PRBs DL usados, a fatia dos PRBs disponíveis e a fatia dos PRBs usados,
# separando transmissões novas e retransmissões. É a medida dos testes T1
# a T4 da PoC (docs/stages/02-multi-slice.md).
#
# A role oai_ran copia este arquivo para a home do lab_user na VM.
#
# Uso:
#   ./slice-prb-share.sh [-s N] [LOG]
#     LOG    log do gNB (padrão: a entrada padrão)
#     -s N   descarta os N primeiros períodos do trecho (aquecimento)
#
# Exemplo, só com o trecho do teste:
#   L=$(wc -l < /tmp/gnb.log)    # antes do tráfego
#   tail -n +"$L" /tmp/gnb.log | ./slice-prb-share.sh -s 5

set -euo pipefail

die() {
  printf 'erro: %s\n' "$1" >&2
  exit 1
}

usage() {
  sed -n '13,20p' "$0" | sed 's/^# \{0,1\}//'
  exit "${1:-0}"
}

skip=0
while getopts ':s:h' opt; do
  case "$opt" in
    s) [[ "$OPTARG" =~ ^[0-9]+$ ]] || die "-s precisa de um número: $OPTARG"
       skip=$OPTARG ;;
    h) usage 0 ;;
    *) usage 1 ;;
  esac
done
shift $((OPTIND - 1))
[[ $# -le 1 ]] || usage 1
if [[ $# -eq 1 ]]; then
  [[ -r "$1" ]] || die "não consigo ler o log: $1"
  exec <"$1"
fi

# grep -a: o log do gNB pode ter bytes que o grep consideraria binários.
# O awk só lê as linhas [SLICE]; o período abre um bloco, e as linhas de
# slice e do grupo padrão que vêm depois pertencem a ele.
grep -a -F '[SLICE]' | LC_ALL=C awk -v skip="$skip" '
  function num_after(s, key,    i, rest) {
    i = index(s, key)
    if (i == 0) return 0
    rest = substr(s, i + length(key))
    sub(/^[^0-9]*/, "", rest)
    return rest + 0
  }
  /\[SLICE\] per/ {
    periods++
    on = periods > skip
    if (on) {
      counted++
      avail += num_after($0, "dispon")
      used += num_after($0, "usados ")
    }
    next
  }
  !on { next }
  /\[SLICE\]   slice / || /\[SLICE\]   grupo padr/ {
    if ($0 ~ /grupo padr/) {
      name = "padrao"
    } else {
      name = $0
      sub(/.*\[SLICE\]   slice /, "", name)
      sub(/ .*/, "", name)
      if (!(name in seen)) {
        quota = $0
        sub(/.*\(min /, "min ", quota)
        sub(/\).*/, "", quota)
        q[name] = quota
      }
    }
    if (!(name in seen)) { seen[name] = 1; order[++n] = name }
    i = index($0, "): ")
    tot[name] += substr($0, i + 3) + 0
    nw[name] += num_after($0, "novas ")
    rt[name] += num_after($0, "retx ")
  }
  END {
    if (counted == 0) {
      print "nenhum período [SLICE] no trecho (o gNB roda com slice_policies?)" > "/dev/stderr"
      exit 1
    }
    printf "períodos: %d (descartados %d)\n", counted, periods - counted
    printf "PRBs DL disponíveis %d, usados %d (%.1f%%)\n", avail, used, avail ? 100 * used / avail : 0
    printf "%-10s %-24s %10s %8s %8s %10s %8s\n", "slice", "cota", "PRBs", "%disp", "%usados", "novas", "retx"
    for (k = 1; k <= n; k++) {
      s = order[k]
      printf "%-10s %-24s %10d %7.1f%% %7.1f%% %10d %8d\n", s, (s in q ? q[s] : "sem cota"), tot[s],
             avail ? 100 * tot[s] / avail : 0, used ? 100 * tot[s] / used : 0, nw[s], rt[s]
    }
  }'
