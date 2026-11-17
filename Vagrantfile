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
        ansible.extra_vars = { common_dns_server: dns_server }
      end
    end
  end
end
