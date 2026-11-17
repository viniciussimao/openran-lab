#!/usr/bin/env bash
#
# preflight.sh: verifica se o host atende aos requisitos do openran-lab
# antes do "vagrant up". Não altera nada no sistema.
#
# Uso: ./scripts/preflight.sh

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG_FILE="${ROOT_DIR}/config.yml"
[[ -f "${CONFIG_FILE}" ]] || CONFIG_FILE="${ROOT_DIR}/config.example.yml"

# Espaço mínimo recomendado no diretório de imagens do libvirt (GB)
MIN_DISK_GB=100
LIBVIRT_IMAGES_DIR="/var/lib/libvirt/images"

errors=0
warnings=0

ok()   { printf '  [ OK ]  %s\n' "$1"; }
warn() { printf '  [AVISO] %s\n' "$1"; warnings=$((warnings + 1)); }
fail() { printf '  [FALHA] %s\n' "$1"; errors=$((errors + 1)); }

# Lê um valor do arquivo de configuração com Python + PyYAML
read_config() {
  python3 - "${CONFIG_FILE}" "$1" <<'PY'
import sys
import yaml

with open(sys.argv[1]) as f:
    cfg = yaml.safe_load(f)

query = sys.argv[2]
vms = [v for v in cfg.get("vms", {}).values() if v.get("enabled")]
if query == "total_cpus":
    print(sum(int(v["cpus"]) for v in vms))
elif query == "total_memory_mb":
    print(sum(int(v["memory_mb"]) for v in vms))
elif query == "network_name":
    print(cfg["management_network"]["name"])
elif query == "network_address":
    print(cfg["management_network"]["address"])
PY
}

echo "openran-lab: verificação do host"
echo "Configuração: ${CONFIG_FILE}"
echo

echo "Hardware"
if [[ -e /dev/kvm ]]; then
  if [[ -r /dev/kvm && -w /dev/kvm ]]; then
    ok "/dev/kvm disponível para o seu usuário"
  else
    fail "/dev/kvm existe, mas o seu usuário não tem acesso (grupo kvm?)"
  fi
else
  fail "/dev/kvm não existe: habilite VT-x/AMD-V na BIOS e instale o KVM"
fi

if grep -q -w avx2 /proc/cpuinfo; then
  ok "CPU com AVX2"
else
  fail "CPU sem AVX2: o OAI não roda corretamente"
fi
if grep -q -w avx512f /proc/cpuinfo; then
  ok "CPU com AVX-512"
fi

if ! python3 -c 'import yaml' 2>/dev/null; then
  fail "python3 com PyYAML não encontrado (instale python3-yaml)"
else
  need_cpus="$(read_config total_cpus)"
  need_mem_mb="$(read_config total_memory_mb)"
  host_cpus="$(nproc)"
  host_mem_mb="$(awk '/MemAvailable/ {print int($2 / 1024)}' /proc/meminfo)"

  if (( host_cpus > need_cpus )); then
    ok "CPUs: ${host_cpus} no host, ${need_cpus} pedidas pelas VMs"
  else
    warn "CPUs: ${host_cpus} no host e ${need_cpus} pedidas; o host vai disputar CPU com as VMs"
  fi

  if (( host_mem_mb > need_mem_mb )); then
    ok "RAM: ${host_mem_mb} MB disponíveis, ${need_mem_mb} MB pedidos"
  else
    fail "RAM: ${host_mem_mb} MB disponíveis e ${need_mem_mb} MB pedidos; reduza memory_mb no config.yml"
  fi
fi

disk_dir="${LIBVIRT_IMAGES_DIR}"
[[ -d "${disk_dir}" ]] || disk_dir="/"
disk_free_gb="$(df -BG --output=avail "${disk_dir}" | tail -1 | tr -dc '0-9')"
if (( disk_free_gb >= MIN_DISK_GB )); then
  ok "Disco: ${disk_free_gb} GB livres em ${disk_dir}"
else
  warn "Disco: ${disk_free_gb} GB livres em ${disk_dir}; recomendado ${MIN_DISK_GB} GB"
fi

echo
echo "Software"
for cmd in virsh vagrant ansible; do
  if command -v "${cmd}" >/dev/null 2>&1; then
    ok "${cmd} instalado"
  else
    fail "${cmd} não encontrado"
  fi
done

if command -v vagrant >/dev/null 2>&1; then
  if vagrant plugin list 2>/dev/null | grep -q '^vagrant-libvirt'; then
    ok "plugin vagrant-libvirt instalado"
  else
    fail "plugin vagrant-libvirt não instalado (vagrant plugin install vagrant-libvirt)"
  fi
fi

echo
echo "Permissões"
for group in libvirt kvm; do
  if id -nG | tr ' ' '\n' | grep -qx "${group}"; then
    ok "usuário no grupo ${group}"
  else
    fail "usuário fora do grupo ${group} (sudo usermod -aG ${group} \$USER e novo login)"
  fi
done

if command -v virsh >/dev/null 2>&1; then
  if virsh -c qemu:///system list >/dev/null 2>&1; then
    ok "acesso ao libvirt (qemu:///system)"
  else
    fail "sem acesso ao qemu:///system"
  fi
fi

echo
echo "Rede de gerenciamento"
if python3 -c 'import yaml' 2>/dev/null && command -v virsh >/dev/null 2>&1; then
  net_name="$(read_config network_name)"
  net_address="$(read_config network_address)"
  net_prefix="${net_address%.*}."

  if virsh -c qemu:///system net-info "${net_name}" >/dev/null 2>&1; then
    ok "rede ${net_name} já existe e será reutilizada"
  elif ip -4 addr show | grep -q "inet ${net_prefix}"; then
    fail "sub-rede ${net_address} já está em uso no host; escolha outra no config.yml"
  else
    ok "rede ${net_name} (${net_address}) será criada pelo Vagrant"
  fi
fi

echo
if (( errors > 0 )); then
  echo "Resultado: ${errors} falha(s) e ${warnings} aviso(s). Corrija as falhas antes do vagrant up."
  exit 1
fi
echo "Resultado: pronto para o vagrant up (${warnings} aviso(s))."
