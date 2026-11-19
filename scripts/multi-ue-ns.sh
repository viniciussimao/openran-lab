#!/usr/bin/env bash
#
# multi-ue-ns.sh: cria e remove os network namespaces dos nrUE da Etapa 2
# na VM ran, um por UE, como no tools/scripts/multi-ue.sh do OAI.
#
# Cada UE roda dentro do namespace ueN e fala com o RFsimulator do gNB pelo
# par veth v-ethN (VM, 10.20N.1.100) / v-ueN (namespace, 10.20N.1.N). Assim
# cada nrUE tem a própria oaitun_ue1, sem colidir com o outro.
# Diferente do script do OAI, este não mexe no iptables: o tráfego do
# RFsimulator vai direto ao endereço da VM no veth, sem NAT.
#
# A role oai_ran copia este arquivo para a home do lab_user na VM.
#
# Uso (como root):
#   sudo ./multi-ue-ns.sh up [N]      cria os namespaces ue1..ueN (padrão: 2)
#   sudo ./multi-ue-ns.sh down [N]    remove os namespaces ue1..ueN
#   ./multi-ue-ns.sh status           lista os namespaces e os endereços
#   ./multi-ue-ns.sh addr N           imprime o endereço do RFsimulator para o UE N
#
# Dentro de um namespace: sudo ip netns exec ue1 <comando>

set -euo pipefail

DEFAULT_COUNT=2
MAX_COUNT=9

die() {
  printf 'erro: %s\n' "$1" >&2
  exit 1
}

usage() {
  sed -n '14,20p' "$0" | sed 's/^# \{0,1\}//'
  exit "${1:-0}"
}

check_count() {
  [[ "$1" =~ ^[1-9]$ ]] || die "número de UEs inválido: $1 (use de 1 a ${MAX_COUNT})"
}

require_root() {
  [[ "$(id -u)" -eq 0 ]] || die "rode como root (sudo)"
}

# Endereço do lado da VM, usado como --rfsimulator.[0].serveraddr pelo UE N
host_addr() {
  printf '10.%d.1.100' "$((200 + $1))"
}

ue_addr() {
  printf '10.%d.1.%d' "$((200 + $1))" "$1"
}

ns_exists() {
  ip netns list | awk '{print $1}' | grep -qx "$1"
}

create_ns() {
  local id="$1"
  local ns="ue${id}"
  local veth_host="v-eth${id}"
  local veth_ue="v-ue${id}"

  if ns_exists "${ns}"; then
    printf '%s já existe, nada a fazer\n' "${ns}"
    return
  fi

  # Sobra de uma execução interrompida: o veth sem o namespace
  if ip link show "${veth_host}" >/dev/null 2>&1; then
    ip link delete "${veth_host}"
  fi

  ip netns add "${ns}"
  ip link add "${veth_host}" type veth peer name "${veth_ue}"
  ip link set "${veth_ue}" netns "${ns}"
  ip addr add "$(host_addr "${id}")/24" dev "${veth_host}"
  ip link set "${veth_host}" up
  ip netns exec "${ns}" ip link set dev lo up
  ip netns exec "${ns}" ip addr add "$(ue_addr "${id}")/24" dev "${veth_ue}"
  ip netns exec "${ns}" ip link set "${veth_ue}" up
  printf '%s criado: UE %s, RFsimulator em %s\n' "${ns}" "$(ue_addr "${id}")" "$(host_addr "${id}")"
}

delete_ns() {
  local id="$1"
  local ns="ue${id}"
  local veth_host="v-eth${id}"

  if ip link show "${veth_host}" >/dev/null 2>&1; then
    ip link delete "${veth_host}"
  fi
  if ns_exists "${ns}"; then
    ip netns delete "${ns}"
    printf '%s removido\n' "${ns}"
  fi
}

show_status() {
  local ns
  local found=0
  while read -r ns _; do
    [[ "${ns}" =~ ^ue[1-9]$ ]] || continue
    found=1
    printf '%s: UE %s, RFsimulator em %s\n' "${ns}" "$(ue_addr "${ns#ue}")" "$(host_addr "${ns#ue}")"
  done < <(ip netns list)
  [[ "${found}" -eq 1 ]] || printf 'nenhum namespace ueN\n'
}

main() {
  [[ $# -ge 1 ]] || usage 1
  local cmd="$1"
  local count="${2:-${DEFAULT_COUNT}}"
  local id

  case "${cmd}" in
    up)
      check_count "${count}"
      require_root
      for ((id = 1; id <= count; id++)); do
        create_ns "${id}"
      done
      ;;
    down)
      check_count "${count}"
      require_root
      for ((id = 1; id <= count; id++)); do
        delete_ns "${id}"
      done
      ;;
    status)
      show_status
      ;;
    addr)
      [[ $# -eq 2 ]] || die "informe o número do UE"
      check_count "$2"
      host_addr "$2"
      printf '\n'
      ;;
    -h | --help | help)
      usage 0
      ;;
    *)
      usage 1
      ;;
  esac
}

main "$@"
