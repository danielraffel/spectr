import unittest

from link_release_downloads import link_downloads


class DownloadLinks(unittest.TestCase):
    def test_versioned_table_links_preserve_prose_and_existing_links(self):
        text = ('| Intel Mac | `Example 1.2.pkg` | Native |\n'
                '| Linux | `Example-linux-x64.tar.xz` | Native |\n'
                '| Other | [existing](https://example.com/file) | |\n'
                'Mention `Example 1.2.pkg` in prose.\n')
        linked = link_downloads(text, 'owner/product', 'v1.2')
        self.assertIn('[Example 1.2.pkg](https://github.com/owner/product/releases/download/v1.2/Example%201.2.pkg)', linked)
        self.assertIn('/v1.2/Example-linux-x64.tar.xz)', linked)
        self.assertIn('[existing](https://example.com/file)', linked)
        self.assertIn('Mention `Example 1.2.pkg` in prose.', linked)
        self.assertEqual(link_downloads(linked, 'owner/product', 'v1.2'), linked)


if __name__ == '__main__':
    unittest.main()
