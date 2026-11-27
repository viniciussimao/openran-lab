# -*- mode: ruby -*-
# vi: set ft=ruby :
#
# Vagrantfile do openran-lab.
# As VMs, os recursos e a rede vêm do config.yml (ou do config.example.yml,
# quando o config.yml ainda não existe). Provider: libvirt/KVM.

require "yaml"
require "ipaddr"

ROOT_DIR = File.dirname(__FILE__)

# Carrega a configuração do servidor
config_path = File.join(ROOT_DIR, "config.yml")
config_path = File.join(ROOT_DIR, "config.example.yml") unless File.exist?(config_path)
settings = YAML.load_file(config_path)

# Prefixo fixo dos nomes das VMs no libvirt: openran-ran, openran-obs, openran-smo
VM_PREFIX = "openran-"

management_network = settings.fetch("management_network")

# O primeiro endereço da rede de gerenciamento é o dnsmasq do libvirt,
# usado como DNS das VMs (veja a role common)
dns_server = IPAddr.new(management_network.fetch("address")).succ.to_s

# Rede privada entre as VMs: isolada, sem DHCP, com o IP fixo de cada VM.
# O primeiro endereço fica com o host.
lab_network = settings.fetch("lab_network") { abort "config: falta lab_network (veja o config.example.yml)" }
lab_subnet = IPAddr.new(lab_network.fetch("address"))
lab_prefix = lab_subnet.prefix
lab_netmask = IPAddr.new("255.255.255.255").mask(lab_prefix).to_s
lab_host_ip = lab_subnet.succ.to_s

# IPs de todas as VMs, habilitadas ou não, para o /etc/hosts (role common).
# Assim, habilitar uma VM nova não muda as que já existem.
lab_hosts = {}
settings.fetch("vms").each do |vm_name, vm_settings|
  ip = vm_settings.fetch("ip") { abort "config: falta o ip da VM #{vm_name} (veja o config.example.yml)" }
  unless lab_subnet.include?(IPAddr.new(ip)) && ip != lab_subnet.to_s && ip != lab_host_ip
    abort "config: o IP #{ip} da VM #{vm_name} precisa estar em #{lab_network.fetch('address')}, " \
          "sem ser o endereço da rede nem o do host (#{lab_host_ip})"
  end
  abort "config: o IP #{ip} está repetido em vms" if lab_hosts.value?(ip)

  lab_hosts[vm_name] = ip
end

Vagrant.configure("2") do |config|
  config.vm.box = settings.fetch("box")
  config.vm.boot_timeout = 600

  # Sem pasta compartilhada: evita depender de NFS no host
  config.vm.synced_folder ".", "/vagrant", disabled: true

  config.vm.provider :libvirt do |libvirt|
    libvirt.default_prefix = VM_PREFIX
    # Repassa a CPU do host (AVX2/AVX-512) para o OAI
    libvirt.cpu_mode = "host-passthrough"
    libvirt.management_network_name = management_network.fetch("name")
    libvirt.management_network_address = management_network.fetch("address")
  end

  settings.fetch("vms").each do |vm_name, vm_settings|
    next unless vm_settings.fetch("enabled", false)

    config.vm.define vm_name do |node|
      node.vm.hostname = vm_name

      # O Vagrant configura a interface (eth1) com o IP fixo e cria a rede
      # no libvirt se ela ainda não existir
      node.vm.network :private_network,
                      ip: vm_settings.fetch("ip"),
                      libvirt__network_name: lab_network.fetch("name"),
                      libvirt__netmask: lab_netmask,
                      libvirt__host_ip: lab_host_ip,
                      libvirt__dhcp_enabled: false,
                      libvirt__forward_mode: "none"

      node.vm.provider :libvirt do |libvirt|
        libvirt.cpus = vm_settings.fetch("cpus")
        libvirt.memory = vm_settings.fetch("memory_mb")
      end

      # Provisionamento com Ansible: cada VM entra no grupo com o seu nome
      node.vm.provision "ansible" do |ansible|
        ansible.compatibility_mode = "2.0"
        ansible.playbook = "ansible/site.yml"
        ansible.config_file = "ansible/ansible.cfg"
        ansible.groups = { vm_name => [vm_name] }
        ansible.extra_vars = { common_dns_server: dns_server, lab_hosts: lab_hosts }
      end
    end
  end
end
