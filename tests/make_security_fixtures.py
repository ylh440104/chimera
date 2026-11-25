import datetime
import hashlib
import ipaddress
import pathlib
import sys
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID


def name(value):
    return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, value)])


def certificate(subject, key, issuer, signer, serial, ca=False, path=None, usage=None, hosts=None, dates=None, signing=True):
    now = datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None)
    interval = (datetime.datetime(2020, 1, 1), datetime.datetime(2040, 1, 1)) if ca else (now - datetime.timedelta(days=1), now + datetime.timedelta(days=90))
    before, after = dates or interval
    builder = x509.CertificateBuilder().subject_name(name(subject)).issuer_name(name(issuer)).public_key(key.public_key()).serial_number(serial).not_valid_before(before).not_valid_after(after)
    builder = builder.add_extension(x509.BasicConstraints(ca, path), True)
    builder = builder.add_extension(x509.KeyUsage(signing, False, not ca, False, False, ca, ca, False, False), True)
    builder = builder.add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), False)
    builder = builder.add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(signer.public_key()), False)
    if usage:
        builder = builder.add_extension(x509.ExtendedKeyUsage(usage), False)
    if hosts:
        builder = builder.add_extension(x509.SubjectAlternativeName(hosts), False)
    return builder.sign(signer, hashes.SHA256())


def main():
    output = pathlib.Path(sys.argv[1]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    root = ec.generate_private_key(ec.SECP256R1())
    intermediate = ec.generate_private_key(ec.SECP256R1())
    leaf = rsa.generate_private_key(65537, 2048)
    unrelated = ec.generate_private_key(ec.SECP256R1())
    weak = rsa.generate_private_key(65537, 1024)
    hosts = [x509.DNSName('chimera.test'), x509.DNSName('*.wild.chimera.test'), x509.IPAddress(ipaddress.ip_address('127.0.0.1'))]
    server = [ExtendedKeyUsageOID.SERVER_AUTH]
    values = {
        'root': certificate('Chimera Test Root', root, 'Chimera Test Root', root, 1, True, 1),
        'intermediate': certificate('Chimera Test Intermediate', intermediate, 'Chimera Test Root', root, 2, True, 0),
        'leaf': certificate('unused-cn.test', leaf, 'Chimera Test Intermediate', intermediate, 3, usage=server, hosts=hosts),
        'same-key': certificate('Second Leaf', leaf, 'Chimera Test Intermediate', intermediate, 4, usage=server, hosts=hosts),
        'unrelated': certificate('Unrelated Root', unrelated, 'Unrelated Root', unrelated, 5, True, 1),
        'expired': certificate('Expired', leaf, 'Chimera Test Intermediate', intermediate, 6, usage=server, hosts=hosts, dates=(datetime.datetime(2000, 1, 1), datetime.datetime(2001, 1, 1))),
        'future': certificate('Future', leaf, 'Chimera Test Intermediate', intermediate, 7, usage=server, hosts=hosts, dates=(datetime.datetime(2045, 1, 1), datetime.datetime(2046, 1, 1))),
        'client': certificate('Client', leaf, 'Chimera Test Intermediate', intermediate, 8, usage=[ExtendedKeyUsageOID.CLIENT_AUTH], hosts=hosts),
        'ec-leaf': certificate('EC Leaf', unrelated, 'Chimera Test Intermediate', intermediate, 9, usage=server, hosts=hosts),
        'nonca-intermediate': certificate('Chimera Test Intermediate', intermediate, 'Chimera Test Root', root, 10, False),
        'short-root': certificate('Chimera Test Root', root, 'Chimera Test Root', root, 11, True, 0),
        'long-lived': certificate('Long Lived', leaf, 'Chimera Test Intermediate', intermediate, 12, usage=server, hosts=hosts, dates=(datetime.datetime(2020, 1, 1), datetime.datetime(2040, 1, 1))),
        'cn-only': certificate('chimera.test', leaf, 'Chimera Test Intermediate', intermediate, 13, usage=server),
        'weak-rsa': certificate('Weak RSA', weak, 'Chimera Test Intermediate', intermediate, 14, usage=server, hosts=hosts),
        'medium-lived': certificate('Medium Lived', leaf, 'Chimera Test Intermediate', intermediate, 15, usage=server, hosts=hosts, dates=(datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(days=1), datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(days=500))),
    }
    for label, value in values.items():
        (output / (label + '.der')).write_bytes(value.public_bytes(serialization.Encoding.DER))
    bad = bytearray((output / 'leaf.der').read_bytes())
    bad[-1] ^= 1
    (output / 'tampered.der').write_bytes(bad)
    system_root = pathlib.Path(__file__).with_name('isrg-root-x1.der').read_bytes()
    if hashlib.sha256(system_root).hexdigest() != '96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6':
        raise RuntimeError('ISRG public root fingerprint mismatch')
    (output / 'system-root.der').write_bytes(system_root)
    print('Wrote', len(values) + 2, 'public certificate fixtures; private keys were not saved')


if __name__ == '__main__':
    main()