import concurrent.futures
import html
import pathlib
import re

import io
import urllib.request
from pypdf import PdfReader

root = pathlib.Path(__file__).parent
page = urllib.request.urlopen('https://jlcpcb.com/partdetail/CY_Changzhou_Chaoyin_Elec-/C968734', timeout=25).read().decode()
links = re.findall(r'href="([^"]+)"', page)
baseline = next(html.unescape(x) for x in links if 'C968734.pdf' in x)
urls = {
    'cy9019': baseline,
    'cpt9019a': 'https://www.sameskydevices.com/product/resource/cpt-9019a-smt-tr.pdf',
    'pam8904e': 'https://www.diodes.com/datasheet/download/PAM8904E.pdf',
    'pui0727': 'https://api.puiaudio.com/filename/SMT-0727-S-R.pdf',
}


def fetch(entry):
    name, url = entry
    try:
        content = urllib.request.urlopen(url, timeout=25).read()
        doc = PdfReader(io.BytesIO(content)).pages
        (root / (name + '.pdf')).write_bytes(content)
        output = [name + ': ' + str(len(doc)) + ' pages']
        for i in ([1] if name == 'cy9019' else [0, 1]):
            if i < len(doc):
                output.append('PAGE ' + str(i + 1) + '\n' + doc[i].extract_text())
        if name == 'pam8904e':
            for p in doc:
                if 'Capacitor Selection' in p.extract_text():
                    output.append(p.extract_text())
        return '\n'.join(output)
    except Exception as e:
        return name + ': ' + type(e).__name__


with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    for result in pool.map(fetch, urls.items()):
        print(result)
