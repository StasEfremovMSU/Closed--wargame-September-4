using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public static class rule1 
{
	static public List <des_point> rule (int num)
	{
		
		List <des_point> res = new List<des_point> ();
		// Если есть приказ на атаку

		// Если полк не рассеян

		// Прокинуть проверку на умность
		int test = Decision.Off_brain_test(100f);
		des_point x;
		switch (test) {
		case (0):
			x = new des_point (1, 100);
			res.Add (x);
			break;

		case (1):
			x = new des_point (1, 60);
			res.Add (x);
			x = new des_point (0, 40);
			res.Add (x);
			break;

		case (2):
			x = new des_point (1, 20);
			res.Add (x);
			 x = new des_point (0, 80);
			res.Add (x);
			break;
		}

		return res;
	}
}
